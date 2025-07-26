#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>

struct Wall {
    float x1, y1, x2, y2;
    bool isPortal = false;
    int adjoiningSector = -1;
};

struct Sector {
    int id;
    std::vector<Wall> walls;
    float floor_height = 0.0f;
    float ceiling_height = 4.0f;
};

static const int WINDOW_W = 800;
static const int WINDOW_H = 600;
static const float VERTEX_RADIUS = 5.0f;
static const float CLOSE_DIST = 10.0f;
static const float SNAP_DIST = 10.0f;
static const float GRID_SIZE = 5.0f;

SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
TTF_Font* font = nullptr;

std::vector<Sector> sectors;
std::vector<SDL_FPoint> currentVertices;
int currentSectorId = 0;

float dist(float x1, float y1, float x2, float y2) {
    return std::sqrt((x2 - x1)*(x2 - x1)+(y2 - y1)*(y2 - y1));
}

bool nearFirstVertex(float x, float y) {
    if (currentVertices.empty()) return false;
    auto &v = currentVertices[0];
    return dist(x,y,v.x,v.y) < CLOSE_DIST;
}

void drawCircle(SDL_Renderer* rend, int x, int y, int radius) {
    for (int w = 0; w < radius*2; w++) {
        for (int h = 0; h < radius*2; h++) {
            int dx = radius - w;
            int dy = radius - h;
            if (dx*dx + dy*dy <= radius*radius) {
                SDL_RenderDrawPoint(rend, x + dx, y + dy);
            }
        }
    }
}

void drawLine(SDL_Renderer* rend, SDL_FPoint a, SDL_FPoint b, bool portal = false) {
    if (portal) {
        const int dashLen = 5;
        float dx = b.x - a.x;
        float dy = b.y - a.y;
        float length = std::sqrt(dx*dx + dy*dy);
        float dashCount = length / (dashLen * 2);
        float dashX = dx / (dashCount * 2);
        float dashY = dy / (dashCount * 2);
        float startX = a.x;
        float startY = a.y;
        for (int i = 0; i < dashCount; ++i) {
            SDL_RenderDrawLine(rend, (int)startX, (int)startY, (int)(startX + dashX), (int)(startY + dashY));
            startX += dashX * 2;
            startY += dashY * 2;
        }
    } else {
        SDL_RenderDrawLine(rend, (int)a.x, (int)a.y, (int)b.x, (int)b.y);
    }
}

void outputMap() {
    printf("# sector_id wall_count floor_height ceiling_height\n");
    for (auto &sec : sectors) {
        printf("%d %lu %.2f %.2f\n", sec.id, sec.walls.size(), sec.floor_height, sec.ceiling_height);
        for (auto &w : sec.walls) {
            printf("%.2f %.2f %.2f %.2f %d %d\n", w.x1, w.y1, w.x2, w.y2, w.isPortal?1:0, w.adjoiningSector);
        }
    }
}

SDL_Texture* renderText(const std::string &message, SDL_Color color) {
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, message.c_str(), color);
    if (!surf) {
        printf("TTF_RenderUTF8_Blended Error: %s\n", TTF_GetError());
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    return texture;
}

void drawText(const std::string &msg, int x, int y) {
    SDL_Color white = {255,255,255,255};
    SDL_Texture* textTex = renderText(msg, white);
    if (!textTex) return;
    int w, h;
    SDL_QueryTexture(textTex, NULL, NULL, &w, &h);
    SDL_Rect dst = {x,y,w,h};
    SDL_RenderCopy(renderer, textTex, NULL, &dst);
    SDL_DestroyTexture(textTex);
}

// New helper: snap a value to the nearest GRID_SIZE multiple
float snapToGrid(float val, float gridSize) {
    return std::round(val / gridSize) * gridSize;
}

// Snap an SDL_FPoint to the grid
SDL_FPoint snapVertexToGrid(const SDL_FPoint& v, float gridSize) {
    return { snapToGrid(v.x, gridSize), snapToGrid(v.y, gridSize) };
}

// Snap new vertex to existing sector vertices if close enough, returns snapped position
SDL_FPoint snapToExistingVertices(float x, float y) {
    for (auto &sec : sectors) {
        for (auto &w : sec.walls) {
            SDL_FPoint v1 = snapVertexToGrid({w.x1, w.y1}, GRID_SIZE);
            SDL_FPoint v2 = snapVertexToGrid({w.x2, w.y2}, GRID_SIZE);
            SDL_FPoint p = snapVertexToGrid({x,y}, GRID_SIZE);
            if (dist(p.x, p.y, v1.x, v1.y) < SNAP_DIST) return v1;
            if (dist(p.x, p.y, v2.x, v2.y) < SNAP_DIST) return v2;
        }
    }
    return snapVertexToGrid({x,y}, GRID_SIZE);
}

// Link portals between new sector walls and existing sector walls, removing duplicates
void linkPortals(Sector& newSector) {
    std::vector<size_t> wallsToRemove;

    for (auto &sec : sectors) {
        if (sec.id == newSector.id) continue;
        for (size_t i = 0; i < newSector.walls.size(); ++i) {
            Wall &newWall = newSector.walls[i];
            for (size_t j = 0; j < sec.walls.size(); ++j) {
                Wall &existWall = sec.walls[j];

                // Snap all endpoints to grid
                SDL_FPoint nw1 = snapVertexToGrid({newWall.x1, newWall.y1}, GRID_SIZE);
                SDL_FPoint nw2 = snapVertexToGrid({newWall.x2, newWall.y2}, GRID_SIZE);
                SDL_FPoint ew1 = snapVertexToGrid({existWall.x1, existWall.y1}, GRID_SIZE);
                SDL_FPoint ew2 = snapVertexToGrid({existWall.x2, existWall.y2}, GRID_SIZE);

                // Check if walls share the same segment (order-independent)
                bool sameSegment = 
                    ((fabs(nw1.x - ew1.x) < 0.001f && fabs(nw1.y - ew1.y) < 0.001f &&
                      fabs(nw2.x - ew2.x) < 0.001f && fabs(nw2.y - ew2.y) < 0.001f) ||
                     (fabs(nw1.x - ew2.x) < 0.001f && fabs(nw1.y - ew2.y) < 0.001f &&
                      fabs(nw2.x - ew1.x) < 0.001f && fabs(nw2.y - ew1.y) < 0.001f));

                if (sameSegment) {
                    existWall.isPortal = true;
                    existWall.adjoiningSector = newSector.id;

                    newWall.isPortal = true;
                    newWall.adjoiningSector = sec.id;

                    wallsToRemove.push_back(i);
                }
            }
        }
    }

    // Remove duplicates from newSector in reverse order
    std::sort(wallsToRemove.begin(), wallsToRemove.end(), std::greater<size_t>());
    for (size_t idx : wallsToRemove) {
        newSector.walls.erase(newSector.walls.begin() + idx);
    }
}


int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init Error: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        printf("TTF_Init Error: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    window = SDL_CreateWindow("Doom-style Sector Editor (SDL2 Software Render + UI + Grid Snap)", 100, 100, WINDOW_W, WINDOW_H, SDL_WINDOW_SHOWN);
    if (!window) {
        printf("SDL_CreateWindow Error: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        printf("SDL_CreateRenderer Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    font = TTF_OpenFont("monospace.ttf", 16);
    if (!font) {
        printf("Failed to load font: %s\n", TTF_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    bool quit = false;
    bool sectorClosed = false;

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch(e.type) {
                case SDL_QUIT:
                    quit = true;
                    break;

                case SDL_MOUSEBUTTONDOWN: {
                    int mx = e.button.x;
                    int my = e.button.y;

                    if (e.button.button == SDL_BUTTON_LEFT) {
                        if (!sectorClosed) {
                            if (nearFirstVertex(mx,my) && currentVertices.size() >= 3) {
                                // Snap all current vertices to existing and to grid
                                for (auto &v : currentVertices) {
                                    SDL_FPoint snapped = snapToExistingVertices(v.x, v.y);
                                    v = snapped;
                                }

                                Sector sec;
                                sec.id = currentSectorId++;
                                sec.floor_height = 0.0f;
                                sec.ceiling_height = 4.0f;
                                // Create walls from vertices
                                for (size_t i = 0; i < currentVertices.size(); ++i) {
                                    size_t next = (i + 1) % currentVertices.size();
                                    Wall w;
                                    w.x1 = currentVertices[i].x;
                                    w.y1 = currentVertices[i].y;
                                    w.x2 = currentVertices[next].x;
                                    w.y2 = currentVertices[next].y;
                                    w.isPortal = false;
                                    w.adjoiningSector = -1;
                                    sec.walls.push_back(w);
                                }

                                linkPortals(sec);

                                sectors.push_back(sec);
                                currentVertices.clear();
                                sectorClosed = true;
                            } else {
                                // Snap the new vertex to grid and existing vertices
                                SDL_FPoint snapped = snapToExistingVertices((float)mx, (float)my);
                                currentVertices.push_back(snapped);
                            }
                        } else {
                            // Start new sector
                            sectorClosed = false;
                            currentVertices.clear();
                            SDL_FPoint snapped = snapToExistingVertices((float)mx, (float)my);
                            currentVertices.push_back(snapped);
                        }
                    }
                } break;

                case SDL_KEYDOWN:
                    if (e.key.keysym.sym == SDLK_RETURN) {
                        outputMap();
                    }
                    break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);

        // Draw existing sectors
        for (auto &sec : sectors) {
            for (auto &w : sec.walls) {
                SDL_SetRenderDrawColor(renderer, w.isPortal ? 0 : 255, w.isPortal ? 255 : 255, 0, 255);
                drawLine(renderer, {w.x1, w.y1}, {w.x2, w.y2}, w.isPortal);
            }
        }

        // Draw current unfinished sector lines and points
        if (!currentVertices.empty()) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            for (size_t i = 0; i < currentVertices.size(); ++i) {
                SDL_FPoint v = currentVertices[i];
                drawCircle(renderer, (int)v.x, (int)v.y, (int)VERTEX_RADIUS);
                if (i > 0) {
                    drawLine(renderer, currentVertices[i-1], v);
                }
            }
            // Draw line from last to mouse pos for preview if sector not closed
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            if (!sectorClosed) {
                drawLine(renderer, currentVertices.back(), { (float)mx, (float)my });
            }
        }

        // Simple UI text
        drawText("Left Click: Add vertex / Close sector (click near start)", 5, 5);
        drawText("Enter: Output map data to console", 5, 25);
        drawText("Vertices snap to a 5px grid for portal alignment", 5, 45);
        drawText("Sectors: " + std::to_string(sectors.size()), 5, 65);

        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    TTF_CloseFont(font);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();

    return 0;
}

