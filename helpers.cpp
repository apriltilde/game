#include <iostream>
#include <SDL2/SDL.h>
#include <vector>
#include <cmath>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>


using namespace std;

// Minimap size
const int MINIMAP_SIZE = 150;
const int MINIMAP_MARGIN = 10;
const double MINIMAP_SCALE = 5.0; // World units to minimap pixels

struct Wall {
    double x1, y1, x2, y2;
    bool isPortal;
    int adjoiningSector; // -1 if solid wall
};

struct Sector {
    vector<Wall> walls;
    double floorHeight = 0.0;
    double ceilingHeight = 3.0;
};

vector<Sector> sectors;

double posX = 2.0, posY = 2.0;
double dirX = -1.0, dirY = 0.0;
double planeX = 0.0, planeY = 0.66;

void drawVerticalLine(SDL_Surface* surface, int x, int start, int end, Uint32 color) {
    for (int y = start; y < end; y++) {
        Uint32* pixels = (Uint32*)surface->pixels;
        pixels[y * (surface->pitch / 4) + x] = color;
    }
}

bool intersectRayWithSegment(double rayX, double rayY, double rayDX, double rayDY,
                              double x1, double y1, double x2, double y2,
                              double& outDist) {
    double rdx = rayDX, rdy = rayDY;
    double sdx = x2 - x1, sdy = y2 - y1;
    double denom = rdx * sdy - rdy * sdx;
    if (fabs(denom) < 1e-6) return false;

    double dx = x1 - rayX;
    double dy = y1 - rayY;
    double t = (dx * sdy - dy * sdx) / denom;
    double u = (dx * rdy - dy * rdx) / denom;

    if (t > 0 && u >= 0 && u <= 1) {
        outDist = t;
        return true;
    }

    return false;
}

int getSectorForPosition(double x, double y) {
    for (int i = 0; i < sectors.size(); ++i) {
        const Sector& sector = sectors[i];
        int crossings = 0;
        for (const Wall& wall : sector.walls) {
            double x1 = wall.x1, y1 = wall.y1;
            double x2 = wall.x2, y2 = wall.y2;

            if (((y1 > y) != (y2 > y)) &&
                (x < (x2 - x1) * (y - y1) / (y2 - y1 + 1e-6) + x1)) {
                crossings++;
            }
        }
        if (crossings % 2 == 1) return i;
    }
    return -1;
}

double pointToSegmentDistance(double px, double py, double x1, double y1, double x2, double y2) {
    double dx = x2 - x1;
    double dy = y2 - y1;

    if (dx == 0 && dy == 0) {
        dx = px - x1;
        dy = py - y1;
        return sqrt(dx*dx + dy*dy);
    }

    double t = ((px - x1) * dx + (py - y1) * dy) / (dx*dx + dy*dy);

    if (t < 0) t = 0;
    else if (t > 1) t = 1;

    double closestX = x1 + t * dx;
    double closestY = y1 + t * dy;

    dx = px - closestX;
    dy = py - closestY;

    return sqrt(dx*dx + dy*dy);
}

const double COLLISION_RADIUS = 0.1;

bool isMovementBlocked(double newX, double newY) {
    int sector = getSectorForPosition(newX, newY);
    if (sector == -1) return true;

    for (const Wall& wall : sectors[sector].walls) {
        if (!wall.isPortal) {
            double dist = pointToSegmentDistance(newX, newY, wall.x1, wall.y1, wall.x2, wall.y2);
            if (dist < COLLISION_RADIUS) {
                return true;
            }
        }
    }

    return false;
}

void loadMapFromFile(const string& filename) {
    ifstream file(filename);
    if (!file.is_open()) {
        cerr << "Failed to open " << filename << endl;
        return;
    }

    sectors.clear();
    string line;
    while (getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        stringstream ss(line);
        int sectorId, wallCount;
        double floorHeight, ceilingHeight;
        if (!(ss >> sectorId >> wallCount >> floorHeight >> ceilingHeight)) continue;

        Sector sector;
        sector.floorHeight = floorHeight;
        sector.ceilingHeight = ceilingHeight;

        for (int i = 0; i < wallCount; ++i) {
            getline(file, line);
            stringstream wallSS(line);
            double x1, y1, x2, y2;
            int isPortalInt, adjoining;
            wallSS >> x1 >> y1 >> x2 >> y2 >> isPortalInt >> adjoining;
            Wall wall = { x1, y1, x2, y2, isPortalInt != 0, adjoining };
            sector.walls.push_back(wall);
        }

        sectors.push_back(sector);
    }

    file.close();
}


void renderMinimap(SDL_Surface* surface) {
    // Draw minimap background (dark grey)
    SDL_Rect bgRect = { MINIMAP_MARGIN, MINIMAP_MARGIN, MINIMAP_SIZE, MINIMAP_SIZE };
    SDL_FillRect(surface, &bgRect, SDL_MapRGB(surface->format, 30, 30, 30));

    // Draw walls and portals
    for (const Sector& sector : sectors) {
        for (const Wall& wall : sector.walls) {
            Uint32 color = wall.isPortal ?
                SDL_MapRGB(surface->format, 0, 255, 255) :  // Cyan for portals
                SDL_MapRGB(surface->format, 255, 255, 255); // White for walls

            int x1 = (int)(wall.x1 * MINIMAP_SCALE) + MINIMAP_MARGIN;
            int x2 = (int)(wall.x2 * MINIMAP_SCALE) + MINIMAP_MARGIN;
			int y1 = (int)(wall.y1 * MINIMAP_SCALE) + MINIMAP_MARGIN;
			int y2 = (int)(wall.y2 * MINIMAP_SCALE) + MINIMAP_MARGIN;

            int dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
            int dy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
            int err = dx + dy, e2;

            int cx = x1, cy = y1;
            while (true) {
                if (cx >= MINIMAP_MARGIN && cx < MINIMAP_MARGIN + MINIMAP_SIZE &&
                    cy >= MINIMAP_MARGIN && cy < MINIMAP_MARGIN + MINIMAP_SIZE) {
                    Uint32* pixels = (Uint32*)surface->pixels;
                    pixels[cy * (surface->pitch / 4) + cx] = color;
                }
                if (cx == x2 && cy == y2) break;
                e2 = 2 * err;
                if (e2 >= dy) {
                    err += dy;
                    cx += sx;
                }
                if (e2 <= dx) {
                    err += dx;
                    cy += sy;
                }
            }
        }
    }

    // Draw player as red circle
    int px = (int)(posX * MINIMAP_SCALE) + MINIMAP_MARGIN;
	int py = (int)(posY * MINIMAP_SCALE) + MINIMAP_MARGIN;	
	Uint32 playerColor = SDL_MapRGB(surface->format, 255, 0, 0);

    const int radius = 4;
    // Simple filled circle
    for (int w = -radius; w <= radius; w++) {
        for (int h = -radius; h <= radius; h++) {
            if (w * w + h * h <= radius * radius) {
                int dx = px + w;
                int dy = py + h;
                if (dx >= MINIMAP_MARGIN && dx < MINIMAP_MARGIN + MINIMAP_SIZE &&
                    dy >= MINIMAP_MARGIN && dy < MINIMAP_MARGIN + MINIMAP_SIZE) {
                    Uint32* pixels = (Uint32*)surface->pixels;
                    pixels[dy * (surface->pitch / 4) + dx] = playerColor;
                }
            }
        }
    }

    // Draw direction line 
    int lineLength = 10;
    int dx = (int)(dirX * lineLength);
    int dy = (int)(dirY * lineLength);

    int xEnd = px + dx;
    int yEnd = py + dy;

    // Draw line on minimap from player position to direction
    int x1 = px, y1 = py;
    int x2 = xEnd, y2 = yEnd;

    int lineDx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int lineDy = -abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = lineDx + lineDy, e2;

    int cx = x1, cy = y1;
    while (true) {
        if (cx >= MINIMAP_MARGIN && cx < MINIMAP_MARGIN + MINIMAP_SIZE &&
            cy >= MINIMAP_MARGIN && cy < MINIMAP_MARGIN + MINIMAP_SIZE) {
            Uint32* pixels = (Uint32*)surface->pixels;
            pixels[cy * (surface->pitch / 4) + cx] = playerColor;
        }
        if (cx == x2 && cy == y2) break;
        e2 = 2 * err;
        if (e2 >= lineDy) {
            err += lineDy;
            cx += sx;
        }
        if (e2 <= lineDx) {
            err += lineDx;
            cy += sy;
        }
    }
}

