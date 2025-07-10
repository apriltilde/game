#include <iostream>
#include <SDL2/SDL.h>
#include <vector>
#include <cmath>
#include <limits>
#include "helpers.h"


using namespace std;

const int SCREEN_WIDTH = 1080;
const int SCREEN_HEIGHT = 720;
const double playerEyeHeightOffset = 1.0; 

void renderFrame(SDL_Surface* surface) {
    int playerSector = getSectorForPosition(posX, posY);
    if (playerSector == -1) return;

    double playerHeight = sectors[playerSector].floorHeight + playerEyeHeightOffset;

    for (int x = 0; x < SCREEN_WIDTH; x++) {
        double cameraX = 2.0 * x / SCREEN_WIDTH - 1;
        double rayDirX = dirX + planeX * cameraX;
        double rayDirY = dirY + planeY * cameraX;

        double rayX = posX, rayY = posY;

        double totalDist = 0.0;
        const int MAX_PORTAL_DEPTH = 10;

        int currentSector = playerSector;

        // Instead of only one sector, we try to find closest wall in all sectors at each step
        for (int depth = 0; depth < MAX_PORTAL_DEPTH; ++depth) {
            double closestDist = numeric_limits<double>::infinity();
            Wall* hitWall = nullptr;
            int hitSectorIndex = -1;

            // Test ray against all sectors (not just currentSector)
            for (int si = 0; si < (int)sectors.size(); si++) {
                Sector* sector = &sectors[si];
                for (Wall& wall : sector->walls) {
                    double dist;
                    if (intersectRayWithSegment(rayX, rayY, rayDirX, rayDirY,
                                                wall.x1, wall.y1, wall.x2, wall.y2, dist)) {
                        if (dist < closestDist) {
                            closestDist = dist;
                            hitWall = &wall;
                            hitSectorIndex = si;
                        }
                    }
                }
            }

            if (!hitWall) break;

            totalDist += closestDist;

            Sector* sector = &sectors[hitSectorIndex];
            double floorHeight = sector->floorHeight;
            double ceilingHeight = sector->ceilingHeight;

            int ceilingScreenY = (int)((SCREEN_HEIGHT / 2.0) - (ceilingHeight - playerHeight) * SCREEN_HEIGHT / totalDist);
            int floorScreenY = (int)((SCREEN_HEIGHT / 2.0) + (playerHeight - floorHeight) * SCREEN_HEIGHT / totalDist);

            ceilingScreenY = max(0, ceilingScreenY);
            floorScreenY = min(SCREEN_HEIGHT - 1, floorScreenY);

            drawVerticalLine(surface, x, 0, ceilingScreenY, SDL_MapRGB(surface->format, 100, 100, 255));

            int lineHeight = (int)(SCREEN_HEIGHT / totalDist);
            int drawStart = ceilingScreenY;
            int drawEnd = floorScreenY;
            if (drawEnd < drawStart) {
                drawStart = max(0, SCREEN_HEIGHT / 2 - lineHeight / 2);
                drawEnd = min(SCREEN_HEIGHT - 1, drawStart + lineHeight);
            }

            Uint32 wallColor = SDL_MapRGB(surface->format,
                                          hitWall->isPortal ? 0 : 255, 105, 180);
            drawVerticalLine(surface, x, drawStart, drawEnd, wallColor);

            drawVerticalLine(surface, x, floorScreenY, SCREEN_HEIGHT, SDL_MapRGB(surface->format, 100, 255, 100));

            if (!hitWall->isPortal) break;

            rayX += rayDirX * (closestDist + 0.01);
            rayY += rayDirY * (closestDist + 0.01);

            currentSector = hitWall->adjoiningSector;
            if (currentSector < 0 || currentSector >= (int)sectors.size()) break;
        }
    }
	//DEBUGGING REMOVE LATER!
    renderMinimap(surface);
}

int main(int argc, char* argv[]) {
    SDL_Window* window = NULL;
    SDL_Surface* screenSurface = NULL;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        cout << SDL_GetError() << endl;
        return 1;
    }

    window = SDL_CreateWindow("Sector & Portal Raycasting with Minimap", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              SCREEN_WIDTH, SCREEN_HEIGHT, SDL_WINDOW_SHOWN);
    if (window == NULL) {
        cout << SDL_GetError() << endl;
        SDL_Quit();
        return 1;
    }

    screenSurface = SDL_GetWindowSurface(window);

    if (argc >= 2) {
        loadMapFromFile(argv[1]);
    } else {
        loadMapFromFile("map.txt");
    }

    bool quit = false;
    SDL_Event e;

    const double moveSpeed = 0.2;
    const double rotSpeed = 0.1;

    while (!quit) {
        const Uint8* keystate = SDL_GetKeyboardState(NULL);

        if (keystate[SDL_SCANCODE_W]) {
            double newX = posX + dirX * moveSpeed;
            double newY = posY + dirY * moveSpeed;
            if (!isMovementBlocked(newX, posY)) posX = newX;
            if (!isMovementBlocked(posX, newY)) posY = newY;
        }
        if (keystate[SDL_SCANCODE_S]) {
            double newX = posX - dirX * moveSpeed;
            double newY = posY - dirY * moveSpeed;
            if (!isMovementBlocked(newX, posY)) posX = newX;
            if (!isMovementBlocked(posX, newY)) posY = newY;
        }
        if (keystate[SDL_SCANCODE_A]) {
            double oldDirX = dirX;
            dirX = dirX * cos(rotSpeed) - dirY * sin(rotSpeed);
            dirY = oldDirX * sin(rotSpeed) + dirY * cos(rotSpeed);
            double oldPlaneX = planeX;
            planeX = planeX * cos(rotSpeed) - planeY * sin(rotSpeed);
            planeY = oldPlaneX * sin(rotSpeed) + planeY * cos(rotSpeed);
        }
        if (keystate[SDL_SCANCODE_D]) {
            double oldDirX = dirX;
            dirX = dirX * cos(-rotSpeed) - dirY * sin(-rotSpeed);
            dirY = oldDirX * sin(-rotSpeed) + dirY * cos(-rotSpeed);
            double oldPlaneX = planeX;
            planeX = planeX * cos(-rotSpeed) - planeY * sin(-rotSpeed);
            planeY = oldPlaneX * sin(-rotSpeed) + planeY * cos(-rotSpeed);
        }

        SDL_FillRect(screenSurface, NULL, SDL_MapRGB(screenSurface->format, 0, 0, 0));
        renderFrame(screenSurface);
        SDL_UpdateWindowSurface(window);

        while (SDL_PollEvent(&e) != 0) {
            if (e.type == SDL_QUIT || (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) {
                quit = true;
            }
        }

        SDL_Delay(16);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

