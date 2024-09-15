#include <stdio.h>
#include <windows.h>
#include <math.h>
#include <time.h>

#define INITIAL_WINDOW_WIDTH 640
#define INITIAL_WINDOW_HEIGHT 480

#define GAME_RES_WIDTH  384
#define GAME_RES_HEIGHT 240
#define GAME_BPP        32
#define GAME_BITMAP_MEM_SIZE (GAME_RES_WIDTH * GAME_RES_HEIGHT * (GAME_BPP / 8))

#define MONITOR_CENTER_H (monitorinfo.rcMonitor.left + monitorinfo.rcMonitor.right)/2
#define MONITOR_CENTER_V (monitorinfo.rcMonitor.top + monitorinfo.rcMonitor.bottom)/2

#define WORLD_SIZE 256

#define SCREEN_SCALING_FACTOR (GAME_RES_WIDTH/2.0)

#define pixel(x, y, colour) if (0 <= x && x < GAME_RES_WIDTH && 0 <= y && y < GAME_RES_HEIGHT) *((int*)buffer.memory + x + y*GAME_RES_WIDTH) = colour

int windowWidth = INITIAL_WINDOW_WIDTH, windowHeight = INITIAL_WINDOW_HEIGHT;
int monitorWidth, monitorHeight;

typedef struct Bitmap {
    BITMAPINFO bitmapInfo;
    void* memory;
} Bitmap;

typedef struct {
    int x;
    int y;
} Point2;

typedef struct {
    float x, y, z;
} Point3;

typedef struct {
    Point3 pos;
    float pitch, yaw;
    float cos_pitch, sin_pitch;
    float cos_yaw, sin_yaw;
    float fov;
    float focal_point;
} Camera;

typedef struct Octant {
    struct Octant* octants;
    int colour;
    char occupancy;
} Octant;

typedef struct {
    Point3 pos;
    float mx, my, mz;
} Ray;

Octant world;
Camera camera;

int running;
Bitmap buffer;
HWND windowHandle;
MONITORINFO monitorinfo = {sizeof(MONITORINFO)};
RECT windowRect;
int lastSizeMsg = 0;

void render();

LRESULT CALLBACK WndProc(HWND WindowHandle, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch(msg) {
        case WM_CLOSE:
            running = 0;
        break;
        case WM_SIZE:
            windowWidth = LOWORD(lParam);
            windowHeight = HIWORD(lParam);
            switch (wParam) {
                case 2:
                    SetWindowLongPtrA(windowHandle, GWL_STYLE, WS_MAXIMIZE | WS_VISIBLE);
                    SetWindowPos(windowHandle, HWND_TOP, monitorinfo.rcMonitor.left, monitorinfo.rcMonitor.top, monitorWidth, monitorHeight, SWP_FRAMECHANGED);
                    break;
                default:
                    SetWindowLongPtrA(windowHandle, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                    windowWidth = INITIAL_WINDOW_WIDTH;
                    windowHeight = INITIAL_WINDOW_HEIGHT;
                    if (lastSizeMsg) {
                        SetWindowPos(windowHandle, HWND_TOP, MONITOR_CENTER_H - INITIAL_WINDOW_WIDTH/2, MONITOR_CENTER_V - INITIAL_WINDOW_HEIGHT/2, INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, SWP_FRAMECHANGED);
                    }
            }
            lastSizeMsg = wParam;
            render();
        break;
        default:
            return DefWindowProc(WindowHandle, msg, wParam, lParam);
    }
    return 0;
}

extern inline int out_of_bounds(Point3 p) {
    return p.x >= WORLD_SIZE || p.x <= -WORLD_SIZE
        || p.y >= WORLD_SIZE || p.y <= -WORLD_SIZE
        || p.z >= WORLD_SIZE || p.z <= -WORLD_SIZE;
}

void update() {
    short escape_key = GetAsyncKeyState(VK_ESCAPE);
    short w = GetAsyncKeyState('W');
    short a = GetAsyncKeyState('A');
    short s = GetAsyncKeyState('S');
    short d = GetAsyncKeyState('D');
    short space_key = GetAsyncKeyState(VK_SPACE);
    short shift_key = GetAsyncKeyState(VK_SHIFT);
    short up_arrow = GetAsyncKeyState(VK_UP);
    short down_arrow = GetAsyncKeyState(VK_DOWN);
    short right_arrow = GetAsyncKeyState(VK_RIGHT);
    short left_arrow = GetAsyncKeyState(VK_LEFT);

    float v = 0.2;

    if (escape_key) SendMessageA(windowHandle, WM_SIZE, 0, 0);
    if (w) {
        camera.pos.x += v*camera.sin_yaw;
        camera.pos.z += -v*camera.cos_yaw;
    }
    if (a) {
        camera.pos.x += -v*camera.cos_yaw;
        camera.pos.z += -v*camera.sin_yaw;
    }
    if (s) {
        camera.pos.x += -v*camera.sin_yaw;
        camera.pos.z += v*camera.cos_yaw;
    }
    if (d) {
        camera.pos.x += v*camera.cos_yaw;
        camera.pos.z += v*camera.sin_yaw;
    }
    if (space_key) camera.pos.y += v;
    if (shift_key) camera.pos.y -= v;

    if (camera.pos.x > (float)WORLD_SIZE) camera.pos.x = (float)WORLD_SIZE;
    if (camera.pos.x < -(float)WORLD_SIZE) camera.pos.x = -(float)WORLD_SIZE;
    if (camera.pos.y > (float)WORLD_SIZE) camera.pos.y = (float)WORLD_SIZE;
    if (camera.pos.y < -(float)WORLD_SIZE) camera.pos.y = -(float)WORLD_SIZE;
    if (camera.pos.z > (float)WORLD_SIZE) camera.pos.z = (float)WORLD_SIZE;
    if (camera.pos.z < -(float)WORLD_SIZE) camera.pos.z = -(float)WORLD_SIZE;

    if (up_arrow) {
        camera.pitch += 0.0349066;
        if (camera.pitch > 1.5708) camera.pitch = 1.5708;
        camera.cos_pitch = cos(camera.pitch);
        camera.sin_pitch = sin(camera.pitch);
    }
    if (down_arrow) {
        camera.pitch -= 0.0349066;
        if (camera.pitch < -1.5708) camera.pitch = -1.5708;
        camera.cos_pitch = cos(camera.pitch);
        camera.sin_pitch = sin(camera.pitch);
    }
    if (right_arrow) {
        camera.yaw += 0.0349066;
        if (camera.yaw > 3.14159) camera.yaw -= 6.28319;
        camera.cos_yaw = cos(camera.yaw);
        camera.sin_yaw = sin(camera.yaw);
    }
    if (left_arrow) {
        camera.yaw -= 0.0349066;
        if (camera.yaw < -3.14159) camera.yaw += 6.28319;
        camera.cos_yaw = cos(camera.yaw);
        camera.sin_yaw = sin(camera.yaw);
    }
}

extern inline float sign(float num) {
    return (num == 0.0) ? NAN : ((num > 0.0) ? 1.0 : -1.0);
}

void next_edge(Ray* ray, int size) {

    float x_diff;
    float y_diff;
    float z_diff;

    if (ray->mx != 0.0) {
        float pos_x = (ray->pos.x == 0.0) ? 0 : sign(ray->pos.x);
        float next_edge_x = pos_x + sign(ray->mx);
        if (next_edge_x > 1.0) next_edge_x--;
        if (next_edge_x < -1.0) next_edge_x++;
        next_edge_x *= (float)size;
        x_diff = fabs((next_edge_x - ray->pos.x) / ray->mx);
    } else {
        x_diff = INFINITY;
    }

    if (ray->my != 0.0) {
        float pos_y = (ray->pos.y == 0.0) ? 0 : sign(ray->pos.y);
        float next_edge_y = pos_y + sign(ray->my);
        if (next_edge_y > 1.0) next_edge_y--;
        if (next_edge_y < -1.0) next_edge_y++;
        next_edge_y *= (float)size;
        y_diff = fabs((next_edge_y - ray->pos.y) / ray->my);
    } else {
        y_diff = INFINITY;
    }

    if (ray->mz != 0.0) {
        float pos_z = (ray->pos.z == 0.0) ? 0 : sign(ray->pos.z);
        float next_edge_z = pos_z + sign(ray->mz);
        if (next_edge_z > 1.0) next_edge_z--;
        if (next_edge_z < -1.0) next_edge_z++;
        next_edge_z *= (float)size;
        z_diff = fabs((next_edge_z - ray->pos.z) / ray->mz);
    } else {
        z_diff = INFINITY;
    }

    float minimum_diff = (x_diff < y_diff) ? ((x_diff < z_diff) ? x_diff:z_diff):((y_diff < z_diff) ? y_diff:z_diff);

    ray->pos.x += ray->mx * minimum_diff;
    ray->pos.y += ray->my * minimum_diff;
    ray->pos.z += ray->mz * minimum_diff;
}

Point3 translate_ray(Ray* ray, int size) {
    Point3 difference;

    if (ray->pos.x == 0.0) {
        if (ray->mx == 0.0)
            difference.x = size;
        else
            difference.x = sign(ray->mx);
    } else difference.x = sign(ray->pos.x)*(float)size;

    if (ray->pos.y == 0.0) {
        if (ray->my == 0.0)
            difference.y = size;
        else
            difference.y = sign(ray->my);
    } else difference.y = sign(ray->pos.y)*(float)size;

    if (ray->pos.z == 0.0) {
        if (ray->mz == 0.0)
            difference.z = size;
        else
            difference.z = sign(ray->mz);
    } else difference.z = sign(ray->pos.z)*(float)size;

    ray->pos.x -= difference.x;
    ray->pos.y -= difference.y;
    ray->pos.z -= difference.z;

    return difference;
}

char index_from_sub_octant(unsigned char sub_octant) {
    char count = 0;
    while (sub_octant != 0) {
        sub_octant >>= 1;
        count++;
    }
    return count-1;
}

extern inline int colour_from_ray(Ray ray) {
    if (ray.pos.x == 0.0) {
        return (ray.mx >= 0.0) ? 0xff0000 : 0xffff;
    } else if (ray.pos.y == 0.0) {
        return (ray.my >= 0.0) ? 0xff00 : 0xff00ff;
    } else if (ray.pos.z == 0.0) {
        return (ray.mz >= 0.0) ? 0xff : 0xffff00;
    } else {
        return 0xffffff;
    }
}

unsigned char find_sub_octant(Ray ray) {
    if (ray.pos.x == 0.0) ray.pos.x = ray.mx;
    if (ray.pos.y == 0.0) ray.pos.y = ray.my;
    if (ray.pos.z == 0.0) ray.pos.z = ray.mz;

    unsigned char octant = 1;
    if (ray.pos.x < 0.0) octant <<= 1;
    if (ray.pos.y < 0.0) octant <<= 2;
    if (ray.pos.z < 0.0) octant <<= 4;
    return octant;
}

int next_voxel_colour(Ray* ray, Octant octant, int size) {

    if (out_of_bounds(ray->pos)) return 0;

    unsigned char sub_octant = find_sub_octant(*ray);

    if (octant.occupancy & sub_octant && size == 1) {
        //return octant.octants[index_from_sub_octant(sub_octant)].colour;
        return colour_from_ray(*ray);
    } else if (octant.occupancy & sub_octant) {
        Point3 difference = translate_ray(ray, size>>1);
        int colour = next_voxel_colour(ray, octant.octants[index_from_sub_octant(sub_octant)], size >> 1);
        ray->pos.x += difference.x;
        ray->pos.y += difference.y;
        ray->pos.z += difference.z;
        return colour;
    } else {
        next_edge(ray, size);
        if (out_of_bounds(ray->pos)) return 0;
        return -1;
    }
}

extern inline Ray rotate_yaw(Ray ray, Camera c) {
    Ray new_ray;

    new_ray.pos.x = ray.pos.x*c.cos_yaw - ray.pos.z*c.sin_yaw;
    new_ray.pos.y = ray.pos.y;
    new_ray.pos.z = ray.pos.x*c.sin_yaw + ray.pos.z*c.cos_yaw;

    new_ray.mx = ray.mx*c.cos_yaw - ray.mz*c.sin_yaw;
    new_ray.my = ray.my;
    new_ray.mz = ray.mz*c.cos_yaw + ray.mx*c.sin_yaw;

    return new_ray;
}

extern inline Ray rotate_pitch(Ray ray, Camera c) {
    Ray new_ray;

    new_ray.pos.x = ray.pos.x;
    new_ray.pos.y = ray.pos.y*c.cos_pitch;
    new_ray.pos.z = ray.pos.y*c.sin_pitch;

    new_ray.mx = ray.mx;
    new_ray.my = ray.my*c.cos_pitch - ray.mz*c.sin_pitch;
    new_ray.mz = ray.mz*c.cos_pitch + ray.my*c.sin_pitch;

    return new_ray;
}

int render_pixel(int screen_x, int screen_y, Octant w, Camera c) {

    Ray ray;

    ray.pos.x = (float)(screen_x - GAME_RES_WIDTH/2)/SCREEN_SCALING_FACTOR;
    ray.pos.y = (float)(screen_y - GAME_RES_HEIGHT/2)/SCREEN_SCALING_FACTOR;

    ray.mx = ray.pos.x;
    ray.my = ray.pos.y;
    ray.mz = -camera.focal_point;

    ray = rotate_pitch(ray, c);
    ray = rotate_yaw(ray, c);

    ray.pos.x += camera.pos.x;
    ray.pos.y += camera.pos.y;
    ray.pos.z += camera.pos.z;

    int colour = next_voxel_colour(&ray, w, WORLD_SIZE);

    while (colour == -1) {
        colour = next_voxel_colour(&ray, w, WORLD_SIZE);
    }

    return colour;
}

void render_world(Octant w, Camera c) {
    for (int y = 0; y < GAME_RES_HEIGHT; y++) {
        for (int x = 0; x < GAME_RES_WIDTH; x++) {
            pixel(x, y, render_pixel(x, y, w, c));
        }
    }
}

void render(HDC hdc) {
    render_world(world, camera);
    StretchDIBits(hdc, 0, 0, windowWidth, windowHeight, 0, 0, GAME_RES_WIDTH, GAME_RES_HEIGHT, buffer.memory, &buffer.bitmapInfo, DIB_RGB_COLORS, SRCCOPY);
}

Octant fill_octant(int size, int fill) {
    Octant octant = (Octant){malloc(8*sizeof(Octant)), 0, 0};

    for (int i = 0; i < 8; i++) {
        if (size == 1) {
            if (fill)
                octant.octants[i] = (Octant){0, 0xff, 1};
            else
                octant.octants[i] = (Octant){0, 0, 0};
        } else {
            if ((i == 2 || i == 3 || i == 6 || i == 7) && size == WORLD_SIZE)
                octant.octants[i] = fill_octant(size>>1, 1);
            else
                octant.octants[i] = fill_octant(size>>1, fill);
        }
        if (octant.octants[i].occupancy) octant.occupancy |= 1 << i;
    }
    
    return octant;
}

int WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, PSTR CmdLine, int CmdShow) {

    world = fill_octant(WORLD_SIZE, 0);

    camera = (Camera){(Point3){0,0,0.1}, 0, 0, 1, 0, 1, 0, 2.0944, 0};
    camera.focal_point = GAME_RES_WIDTH/2/tan(camera.fov/2)/SCREEN_SCALING_FACTOR;

    buffer.bitmapInfo.bmiHeader.biSize = sizeof(buffer.bitmapInfo.bmiHeader);
    buffer.bitmapInfo.bmiHeader.biWidth = GAME_RES_WIDTH;
    buffer.bitmapInfo.bmiHeader.biHeight = GAME_RES_HEIGHT;
    buffer.bitmapInfo.bmiHeader.biBitCount = GAME_BPP;
    buffer.bitmapInfo.bmiHeader.biCompression = BI_RGB;
    buffer.bitmapInfo.bmiHeader.biPlanes = 1;
    buffer.memory = VirtualAlloc(NULL, GAME_BITMAP_MEM_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    if (buffer.memory == NULL) {
        MessageBox(NULL, "Failed to Allocate Memory", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }
    
    if (!GetMonitorInfoA(MonitorFromWindow(windowHandle, MONITOR_DEFAULTTOPRIMARY), &monitorinfo)) {
        MessageBox(NULL, "Monitor info failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    monitorWidth = monitorinfo.rcMonitor.right - monitorinfo.rcMonitor.left;
    monitorHeight = monitorinfo.rcMonitor.bottom - monitorinfo.rcMonitor.top;

    WNDCLASSEXA WindowClass;

    WindowClass.cbSize = sizeof(WNDCLASSEX);
    WindowClass.style = 0;
    WindowClass.lpfnWndProc = WndProc;
    WindowClass.cbClsExtra = 0;
    WindowClass.cbWndExtra = 0;
    WindowClass.hInstance = Instance;
    WindowClass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    WindowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    WindowClass.hbrBackground = CreateSolidBrush(RGB(255, 0, 255)); // (HBRUSH)COLOR_WINDOW;
    WindowClass.lpszMenuName = NULL;
    WindowClass.lpszClassName = "Game";
    WindowClass.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

    if (!RegisterClassEx(&WindowClass)) {
        MessageBox(NULL, "Window Registration Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    windowHandle = CreateWindowEx(0, WindowClass.lpszClassName, "Window Title", WS_OVERLAPPEDWINDOW | WS_VISIBLE, MONITOR_CENTER_H - INITIAL_WINDOW_WIDTH/2, MONITOR_CENTER_V - INITIAL_WINDOW_HEIGHT/2, INITIAL_WINDOW_WIDTH, INITIAL_WINDOW_HEIGHT, NULL, NULL, Instance, NULL);

    if (windowHandle == NULL) {
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    UpdateWindow(windowHandle);

    HDC hdc = GetDC(windowHandle);

    running = 1;
    MSG Msg;

    float timer = (float)clock() / CLOCKS_PER_SEC;
    int frames = 0;

    float last_time = (float)clock() / CLOCKS_PER_SEC;
    float ups = 20;
    float fraction_of_update = 0;
    int updates = 0;

    char window_name[30];

    while (running) {
        while (PeekMessageA(&Msg, NULL, 0, 0, PM_REMOVE)) {
            DispatchMessageA(&Msg);
        }

        float now = (float)clock() / CLOCKS_PER_SEC;
        fraction_of_update += (now - last_time) * ups;
        last_time = now;
        while (fraction_of_update >= 1.0) {
            update();
            updates++;
            fraction_of_update--;
        }

        render(hdc);
        frames++;

        if ((float)clock()/CLOCKS_PER_SEC - timer > 1.0) {
            timer++;
            sprintf(window_name, "%d | %d", frames, updates);
            SetWindowTextA(windowHandle, window_name);
            frames = 0;
            updates = 0;
        }
    }

    ReleaseDC(windowHandle, hdc);

    return 0;
}
