#include <SDL2/SDL.h>
#include <vector>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <string>

const int GRID_SIZE = 32;
const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const float HIT_RADIUS = 6.0f;

struct Point {
    int x, y;
};

struct Wall {
    Point p1, p2;
    bool isPortal = false;
    int adjoiningSector = -1;
};

struct Sector {
    std::vector<Wall> walls;
    float floorHeight = 0.0f;
    float ceilingHeight = 4.0f;
};

std::vector<Sector> sectors;
std::vector<Point> currentWallPoints;
std::string currentMapFilename = "map.txt";  // Default save filename

SDL_Point gridSnap(int x, int y) {
    return {
        (x / GRID_SIZE) * GRID_SIZE,
        (y / GRID_SIZE) * GRID_SIZE
    };
}

void drawGrid(SDL_Renderer* renderer) {
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    for (int x = 0; x < SCREEN_WIDTH; x += GRID_SIZE)
        SDL_RenderDrawLine(renderer, x, 0, x, SCREEN_HEIGHT);
    for (int y = 0; y < SCREEN_HEIGHT; y += GRID_SIZE)
        SDL_RenderDrawLine(renderer, 0, y, SCREEN_WIDTH, y);
}

void drawWalls(SDL_Renderer* renderer, const std::vector<Wall>& walls) {
    for (const Wall& wall : walls) {
        if (wall.isPortal) {
            SDL_SetRenderDrawColor(renderer, 0, 128, 255, 255); // Blue portals
        } else {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255); // White normal walls
        }
        SDL_RenderDrawLine(renderer, wall.p1.x, wall.p1.y, wall.p2.x, wall.p2.y);
    }
}

void drawCurrent(SDL_Renderer* renderer) {
    if (currentWallPoints.size() < 2) return;
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    for (size_t i = 0; i < currentWallPoints.size() - 1; ++i) {
        SDL_RenderDrawLine(renderer,
                           currentWallPoints[i].x, currentWallPoints[i].y,
                           currentWallPoints[i + 1].x, currentWallPoints[i + 1].y);
    }
}

bool wallsExactlyMatch(const Wall& a, const Wall& b) {
    return (a.p1.x == b.p1.x && a.p1.y == b.p1.y && a.p2.x == b.p2.x && a.p2.y == b.p2.y) ||
           (a.p1.x == b.p2.x && a.p1.y == b.p2.y && a.p2.x == b.p1.x && a.p2.y == b.p1.y);
}


void saveMap(const std::string& filename) {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to save map to " << filename << "\n";
        return;
    }

    for (size_t i = 0; i < sectors.size(); ++i) {
        const Sector& sec = sectors[i];
        out << i << " " << sec.walls.size() << " "
            << sec.floorHeight << " " << sec.ceilingHeight << "\n";

        for (const Wall& wall : sec.walls) {
            out << wall.p1.x / GRID_SIZE << " "
                << wall.p1.y / GRID_SIZE << " "
                << wall.p2.x / GRID_SIZE << " "
                << wall.p2.y / GRID_SIZE << " "
                << wall.isPortal << " "
                << wall.adjoiningSector << "\n";
        }
        out << "\n";
    }

    std::cout << "Map saved to " << filename << "\n";
}

bool loadMap(const char* filename) {
    std::ifstream in(filename);
    if (!in) {
        std::cerr << "Failed to open map file: " << filename << "\n";
        return false;
    }

    sectors.clear();

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;

        std::istringstream header(line);
        int sectorID, wallCount;
        float floorH, ceilH;
        if (!(header >> sectorID >> wallCount >> floorH >> ceilH)) {
            std::cerr << "Malformed sector header: " << line << "\n";
            return false;
        }

        Sector sector;
        sector.floorHeight = floorH;
        sector.ceilingHeight = ceilH;

        for (int i = 0; i < wallCount; ++i) {
            if (!std::getline(in, line)) {
                std::cerr << "Unexpected EOF reading walls\n";
                return false;
            }
            std::istringstream wallLine(line);
            int x1, y1, x2, y2;
            int isPortalInt, adjoiningSector;
            if (!(wallLine >> x1 >> y1 >> x2 >> y2 >> isPortalInt >> adjoiningSector)) {
                std::cerr << "Malformed wall line: " << line << "\n";
                return false;
            }
            Wall w;
            w.p1 = {x1 * GRID_SIZE, y1 * GRID_SIZE};
            w.p2 = {x2 * GRID_SIZE, y2 * GRID_SIZE};
            w.isPortal = (isPortalInt != 0);
            w.adjoiningSector = adjoiningSector;
            sector.walls.push_back(w);
        }
        sectors.push_back(sector);

        std::getline(in, line); // blank line between sectors
    }

    std::cout << "Loaded map from " << filename << " with " << sectors.size() << " sectors.\n";
    currentMapFilename = filename;  // Save loaded filename for later save
    return true;
}

float distanceToSegment(Point p, Point v, Point w) {
    float l2 = std::pow(w.x - v.x, 2) + std::pow(w.y - v.y, 2);
    if (l2 == 0.0f) return std::hypot(p.x - v.x, p.y - v.y);

    float t = ((p.x - v.x) * (w.x - v.x) + (p.y - v.y) * (w.y - v.y)) / l2;
    t = std::max(0.0f, std::min(1.0f, t));
    float projX = v.x + t * (w.x - v.x);
    float projY = v.y + t * (w.y - v.y);
    return std::hypot(p.x - projX, p.y - projY);
}

struct WallRef {
    Wall* wall;
    int sectorIndex;
};

WallRef findWallAt(int x, int y) {
    Point p = {x, y};
    for (size_t i = 0; i < sectors.size(); ++i) {
        Sector& sector = sectors[i];
        for (Wall& wall : sector.walls) {
            if (distanceToSegment(p, wall.p1, wall.p2) < HIT_RADIUS) {
                return { &wall, static_cast<int>(i) };
            }
        }
    }
    return { nullptr, -1 };
}

float cross(const Point& a, const Point& b, const Point& c) {
    return (b.x - a.x)*(c.y - a.y) - (b.y - a.y)*(c.x - a.x);
}

bool segmentsOverlap(const Point& p1, const Point& q1, const Point& p2, const Point& q2) {
    if (std::abs(cross(p1, q1, p2)) > 0.1f) return false;
    if (std::abs(cross(p1, q1, q2)) > 0.1f) return false;

    bool xOverlap = !(std::max(p1.x, q1.x) < std::min(p2.x, q2.x) || std::min(p1.x, q1.x) > std::max(p2.x, q2.x));
    bool yOverlap = !(std::max(p1.y, q1.y) < std::min(p2.y, q2.y) || std::min(p1.y, q1.y) > std::max(p2.y, q2.y));

    return xOverlap && yOverlap;
}

void tryAutoLinkPortal(Wall* wall, int sectorIndex) {
    Sector& currentSector = sectors[sectorIndex];

    for (size_t i = 0; i < sectors.size(); ++i) {
        if (i == sectorIndex) continue;

        Sector& otherSector = sectors[i];

        for (size_t j = 0; j < otherSector.walls.size(); ++j) {
            Wall& otherWall = otherSector.walls[j];

            if (otherWall.isPortal) continue;

            // Check for overlap
            if (segmentsOverlap(wall->p1, wall->p2, otherWall.p1, otherWall.p2)) {
                // Determine overlapping segment
                Point a1 = wall->p1, a2 = wall->p2;
                Point b1 = otherWall.p1, b2 = otherWall.p2;

                // Normalize to same direction for consistent math
                if (a1.x > a2.x || (a1.x == a2.x && a1.y > a2.y)) std::swap(a1, a2);
                if (b1.x > b2.x || (b1.x == b2.x && b1.y > b2.y)) std::swap(b1, b2);

                Point overlapStart = { std::max(a1.x, b1.x), std::max(a1.y, b1.y) };
                Point overlapEnd   = { std::min(a2.x, b2.x), std::min(a2.y, b2.y) };

                // Reconstruct wall into new segments
                std::vector<Wall> newWalls;
                if (!(a1.x == overlapStart.x && a1.y == overlapStart.y))
                    newWalls.push_back({ a1, overlapStart, false, -1 });
                newWalls.push_back({ overlapStart, overlapEnd, true, (int)i });
                if (!(a2.x == overlapEnd.x && a2.y == overlapEnd.y))
                    newWalls.push_back({ overlapEnd, a2, false, -1 });

                // Replace wall in current sector
                for (size_t k = 0; k < currentSector.walls.size(); ++k) {
                    if (&currentSector.walls[k] == wall) {
                        currentSector.walls.erase(currentSector.walls.begin() + k);
                        currentSector.walls.insert(currentSector.walls.begin() + k, newWalls.begin(), newWalls.end());
                        break;
                    }
                }

                // Reconstruct other wall
                std::vector<Wall> newOtherWalls;
                if (!(b1.x == overlapStart.x && b1.y == overlapStart.y))
                    newOtherWalls.push_back({ b1, overlapStart, false, -1 });
                newOtherWalls.push_back({ overlapStart, overlapEnd, true, sectorIndex });
                if (!(b2.x == overlapEnd.x && b2.y == overlapEnd.y))
                    newOtherWalls.push_back({ overlapEnd, b2, false, -1 });

                // Replace other wall in other sector
                otherSector.walls.erase(otherSector.walls.begin() + j);
                otherSector.walls.insert(otherSector.walls.begin() + j, newOtherWalls.begin(), newOtherWalls.end());

                std::cout << "Auto-linked portal between sector " << sectorIndex << " and " << i << "\n";
                return;
            }
        }
    }
}

   
// Find which sector is hovered by mouse (close to any wall)
int findHoveredSector(int mx, int my) {
    Point mouseP = {mx, my};
    for (size_t i = 0; i < sectors.size(); ++i) {
        const Sector& sec = sectors[i];
        for (const Wall& w : sec.walls) {
            if (distanceToSegment(mouseP, w.p1, w.p2) < HIT_RADIUS) {
                return (int)i;
            }
        }
    }
    return -1;
}

void deleteSector(int sectorIndex) {
    if (sectorIndex < 0 || sectorIndex >= (int)sectors.size()) return;

    // Remove all portals in other sectors that link to this sector
    for (Sector& sec : sectors) {
        for (Wall& w : sec.walls) {
            if (w.isPortal && w.adjoiningSector == sectorIndex) {
                w.isPortal = false;
                w.adjoiningSector = -1;
            } else if (w.isPortal && w.adjoiningSector > sectorIndex) {
                // Shift sector indexes down by one due to removal
                w.adjoiningSector--;
            }
        }
    }

    // Remove the sector
    sectors.erase(sectors.begin() + sectorIndex);

    // Fix all portals with adjoiningSector indexes higher than removed sector
    for (Sector& sec : sectors) {
        for (Wall& w : sec.walls) {
            if (w.isPortal && w.adjoiningSector > sectorIndex) {
                w.adjoiningSector--;
            }
        }
    }

    std::cout << "Deleted sector " << sectorIndex << "\n";
}

int main(int argc, char* argv[]) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cerr << "SDL_Init error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Grid Map Editor",
                                          SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (!window) {
        std::cerr << "SDL_CreateWindow error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer error: " << SDL_GetError() << std::endl;
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    if (argc >= 2) {
        if (!loadMap(argv[1])) {
            std::cerr << "Failed to load map. Starting empty editor.\n";
        }
    }

    bool running = true;
    SDL_Event event;

    while (running) {
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                running = false;
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    SDL_Point snapped = gridSnap(event.button.x, event.button.y);
                    currentWallPoints.push_back({snapped.x, snapped.y});
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    WallRef ref = findWallAt(event.button.x, event.button.y);
                    if (ref.wall) {
                        if (!ref.wall->isPortal) {
                            ref.wall->isPortal = true;
                            tryAutoLinkPortal(ref.wall, ref.sectorIndex);
                        } else {
                            int linkedSector = ref.wall->adjoiningSector;
                            if (linkedSector >= 0 && linkedSector < (int)sectors.size()) {
                                Sector& linkedSec = sectors[linkedSector];
                                for (Wall& w : linkedSec.walls) {
                                    if (w.adjoiningSector == ref.sectorIndex && w.isPortal) {
                                        w.isPortal = false;
                                        w.adjoiningSector = -1;
                                    }
                                }
                            }
                            ref.wall->isPortal = false;
                            ref.wall->adjoiningSector = -1;
                            std::cout << "Portal unset on both sides.\n";
                        }
                    }
                }
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_RETURN && currentWallPoints.size() > 2) {
                    Sector newSector;
                    for (size_t i = 0; i < currentWallPoints.size(); ++i) {
                        Point p1 = currentWallPoints[i];
                        Point p2 = currentWallPoints[(i + 1) % currentWallPoints.size()];
                        newSector.walls.push_back({p1, p2});
                    }
                    sectors.push_back(newSector);
                    currentWallPoints.clear();
                    std::cout << "Sector created. Total sectors: " << sectors.size() << "\n";
                }
                else if (event.key.keysym.sym == SDLK_s) {
                    saveMap(currentMapFilename);
                }
                else if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                }
                else if (event.key.keysym.sym == SDLK_d) {
                    int mx, my;
                    SDL_GetMouseState(&mx, &my);
                    int hovered = findHoveredSector(mx, my);
                    if (hovered >= 0) {
                        deleteSector(hovered);
                    }
                }
                break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);

        drawGrid(renderer);
        for (const Sector& s : sectors)
            drawWalls(renderer, s.walls);
        drawCurrent(renderer);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}

