// helpers.h
#ifndef HELPERS_H
#define HELPERS_H

#include <SDL2/SDL.h>
#include <vector>
#include <string>

struct Wall {
    double x1, y1, x2, y2;
    bool isPortal;
    int adjoiningSector; // -1 if solid wall
};

struct Sector {
    std::vector<Wall> walls;
    double floorHeight = 0.0;
    double ceilingHeight = 3.0;
};

extern std::vector<Sector> sectors;

extern double posX, posY;
extern double dirX, dirY;
extern double planeX, planeY;

int getSectorForPosition(double x, double y);
double pointToSegmentDistance(double px, double py, double x1, double y1, double x2, double y2);
bool isMovementBlocked(double newX, double newY);
bool intersectRayWithSegment(double rayX, double rayY, double rayDX, double rayDY,
                              double x1, double y1, double x2, double y2,
                              double& outDist);
void drawVerticalLine(SDL_Surface* surface, int x, int start, int end, Uint32 color);
void renderMinimap(SDL_Surface* surface);
void loadMapFromFile(const std::string& filename);

#endif 
