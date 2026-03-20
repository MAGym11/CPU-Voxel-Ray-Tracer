#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <wchar.h>
#include <windows.h>
#include <math.h>
#include <time.h>
#include <wingdi.h>

#define INITIAL_WINDOW_WIDTH 768
#define INITIAL_WINDOW_HEIGHT 432

#define GAME_RES_WIDTH  384
#define GAME_RES_HEIGHT 216
#define GAME_BPP        16
#define GAME_BITMAP_MEM_SIZE (GAME_RES_WIDTH * GAME_RES_HEIGHT * (GAME_BPP / 8))

#define MONITOR_CENTER_H (monitorinfo.rcMonitor.left + monitorinfo.rcMonitor.right)/2
#define MONITOR_CENTER_V (monitorinfo.rcMonitor.top + monitorinfo.rcMonitor.bottom)/2

#define WORLD_SCALE 1024

#define SCREEN_SCALING_FACTOR (GAME_RES_WIDTH/2.0)

#define pixel(x, y, colour) *((uint16_t*)buffer.memory + x + y*GAME_RES_WIDTH) = colour

#define SKY 0x475d

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

typedef struct Voxel {
    unsigned short colour: 15;
    unsigned short occupancy: 1;
} Voxel;

typedef struct Brick {
    union {
        uint64_t brick_array_index;
        uint64_t voxel_array_index;
    };
    uint64_t occupancy;
} Brick;

typedef struct {
    Point3 pos;
    float dir_x, dir_y, dir_z, inv_dir_x, inv_dir_y, inv_dir_z;
    int collisions;
    uint16_t colour;
} Ray;

Camera camera;

Brick* brick_array;
Voxel* voxel_array;

HDC hdc;
int running;
Bitmap buffer;
int initial_window_width;
int initial_window_height;
HWND windowHandle;
MONITORINFO monitorinfo = {sizeof(MONITORINFO)};
RECT windowRect;
int lastSizeMsg = 0;

float ray_x_pos_array[GAME_RES_WIDTH];
float ray_y_pos_array[GAME_RES_HEIGHT];

Ray start_rays[GAME_RES_WIDTH*GAME_RES_HEIGHT];

int16_t height_map[WORLD_SCALE*WORLD_SCALE];

float light_height;

void render(HDC hdc);

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
                    windowWidth = windowRect.right - windowRect.left;
                    windowHeight = windowRect.bottom - windowRect.top;
                    if (lastSizeMsg) {
                        SetWindowPos(windowHandle, HWND_TOP, windowRect.left, windowRect.top, windowWidth, windowHeight, SWP_FRAMECHANGED);
                    }
            }
            lastSizeMsg = wParam;
            render(hdc);
        break;
        default:
            return DefWindowProc(WindowHandle, msg, wParam, lParam);
    }
    return 0;
}

extern inline Ray rotate_yaw(Ray ray, Camera c) {
    Ray new_ray;

    new_ray.pos.x = ray.pos.x*c.cos_yaw - ray.pos.z*c.sin_yaw;
    new_ray.pos.y = ray.pos.y;
    new_ray.pos.z = ray.pos.x*c.sin_yaw + ray.pos.z*c.cos_yaw;

    new_ray.dir_x = ray.dir_x*c.cos_yaw - ray.dir_z*c.sin_yaw;
    new_ray.dir_y = ray.dir_y;
    new_ray.dir_z = ray.dir_z*c.cos_yaw + ray.dir_x*c.sin_yaw;

    new_ray.inv_dir_x = 1.0f / new_ray.dir_x;
    new_ray.inv_dir_y = 1.0f / new_ray.dir_y;
    new_ray.inv_dir_z = 1.0f / new_ray.dir_z;

    return new_ray;
}

extern inline Ray rotate_pitch(Ray ray, Camera c) {
    Ray new_ray;

    new_ray.pos.x = ray.pos.x;
    new_ray.pos.y = ray.pos.y*c.cos_pitch;
    new_ray.pos.z = ray.pos.y*c.sin_pitch;

    new_ray.dir_x = ray.dir_x;
    new_ray.dir_y = new_ray.pos.y - ray.dir_z*c.sin_pitch;
    new_ray.dir_z = ray.dir_z*c.cos_pitch + new_ray.pos.z;

    return new_ray;
}

void update_start_rays(char camera_moved_xz, char camera_moved_y, char camera_rotated) {
    for (int y = 0; y < GAME_RES_HEIGHT; y++) {
        for (int x = 0; x < GAME_RES_WIDTH; x++) {
            if (camera_rotated) {
                start_rays[x + y*GAME_RES_WIDTH].pos.x = ray_x_pos_array[x];
                start_rays[x + y*GAME_RES_WIDTH].pos.y = ray_y_pos_array[y];

                start_rays[x + y*GAME_RES_WIDTH].dir_x = ray_x_pos_array[x];
                start_rays[x + y*GAME_RES_WIDTH].dir_y = ray_y_pos_array[y];
                start_rays[x + y*GAME_RES_WIDTH].dir_z = -camera.focal_point;

                start_rays[x + y*GAME_RES_WIDTH] = rotate_pitch(start_rays[x + y*GAME_RES_WIDTH], camera);
                start_rays[x + y*GAME_RES_WIDTH] = rotate_yaw(start_rays[x + y*GAME_RES_WIDTH], camera);

                start_rays[x + y*GAME_RES_WIDTH].collisions = 0;
                start_rays[x + y*GAME_RES_WIDTH].colour = 0;
            }
            if (camera_moved_xz || camera_rotated) {
                start_rays[x + y*GAME_RES_WIDTH].pos.x = start_rays[x + y*GAME_RES_WIDTH].dir_x + camera.pos.x;
                start_rays[x + y*GAME_RES_WIDTH].pos.z = start_rays[x + y*GAME_RES_WIDTH].dir_z + camera.pos.z;
            }
            if (camera_moved_y || camera_rotated) {
                start_rays[x + y*GAME_RES_WIDTH].pos.y = start_rays[x + y*GAME_RES_WIDTH].dir_y + camera.pos.y;
            }
        }
    }
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

    char camera_moved_xz = 0;
    char camera_moved_y = 0;
    char camera_rotated = 0;


    light_height -= WORLD_SCALE*0.005f;
    if (light_height <= 0) light_height += WORLD_SCALE;

    float v = WORLD_SCALE*0.0125f;

    if (escape_key) SendMessageA(windowHandle, WM_SIZE, 0, 0);

    if (up_arrow) {
        camera.pitch += 0.0698132;
        camera_rotated = 1;
        if (camera.pitch > 1.5708) camera.pitch = 1.5708;
        camera.cos_pitch = cos(camera.pitch);
        camera.sin_pitch = sin(camera.pitch);
    }
    if (down_arrow) {
        camera.pitch -= 0.0698132;
        camera_rotated = 1;
        if (camera.pitch < -1.5708) camera.pitch = -1.5708;
        camera.cos_pitch = cos(camera.pitch);
        camera.sin_pitch = sin(camera.pitch);
    }
    if (right_arrow) {
        camera.yaw += 0.0698132;
        camera_rotated = 1;
        if (camera.yaw > 3.14159) camera.yaw -= 6.28319;
        camera.cos_yaw = cos(camera.yaw);
        camera.sin_yaw = sin(camera.yaw);
    }
    if (left_arrow) {
        camera.yaw -= 0.0698132;
        camera_rotated = 1;
        if (camera.yaw < -3.14159) camera.yaw += 6.28319;
        camera.cos_yaw = cos(camera.yaw);
        camera.sin_yaw = sin(camera.yaw);
    }

    if (w) {
        camera.pos.x += v*camera.sin_yaw;
        camera.pos.z += -v*camera.cos_yaw;
        camera_moved_xz = 1;
    }
    if (a) {
        camera.pos.x += -v*camera.cos_yaw;
        camera.pos.z += -v*camera.sin_yaw;
        camera_moved_xz = 1;
    }
    if (s) {
        camera.pos.x += -v*camera.sin_yaw;
        camera.pos.z += v*camera.cos_yaw;
        camera_moved_xz = 1;
    }
    if (d) {
        camera.pos.x += v*camera.cos_yaw;
        camera.pos.z += v*camera.sin_yaw;
        camera_moved_xz = 1;
    }
    if (space_key) {
        camera.pos.y += v;
        camera_moved_y = 1;
    }
    if (shift_key) {
        camera.pos.y -= v;
        camera_moved_y = 1;
    }

    if (camera.pos.x > WORLD_SCALE) camera.pos.x = WORLD_SCALE;
    if (camera.pos.x < 0) camera.pos.x = 0;
    if (camera.pos.y > WORLD_SCALE) camera.pos.y = WORLD_SCALE;
    if (camera.pos.y < 0) camera.pos.y = 0;
    if (camera.pos.z > WORLD_SCALE) camera.pos.z = WORLD_SCALE;
    if (camera.pos.z < 0) camera.pos.z = 0;

    if (camera_moved_xz
            || camera_moved_y
            || camera_rotated) update_start_rays(camera_moved_xz, camera_moved_y, camera_rotated);
}

extern inline uint32_t sign(float num) {
    return (signbit(num)) ? -1 : 1;
}

float inv_modulus(float x, float y, float z) {
    return 1.0f / sqrt(x*x + y*y + z*z);
}

int is_integer(float num) {
    int truncated = (int)num;
    return num == truncated;
}

Point3 get_normal(Ray ray) {
    if (is_integer(ray.pos.x)) {
        return (ray.dir_x >= 0.0) ? (Point3){-1, 0, 0} : (Point3){1, 0, 0};
    } else if (is_integer(ray.pos.y)) {
        return (ray.dir_y >= 0.0) ? (Point3){0, -1, 0} : (Point3){0, 1, 0};
    } else if (is_integer(ray.pos.z)) {
        return (ray.dir_z >= 0.0) ? (Point3){0, 0, -1} : (Point3){0, 0, 1};
    } else {
        return (Point3){0, 0, 0};
    }
}

extern inline float colour_from_ray(Ray ray) {
    Point3 normal = get_normal(ray);
    float dot_product = (light_height - ray.pos.x) * normal.x
                        + ((WORLD_SCALE>>1) - ray.pos.y) * normal.y
                        + (light_height - ray.pos.z) * normal.z;
    dot_product *= inv_modulus((light_height - ray.pos.x),
                                ((WORLD_SCALE>>1) - ray.pos.y),
                                (light_height - ray.pos.z));
    return dot_product * 0.5 + 0.5;
}

extern inline uint16_t scale_colour(uint16_t c, float scaler) {
    uint8_t r = (c & 0x1f) * scaler;
    uint8_t g = ((c>>5) & 0x1f) * scaler;
    uint8_t b = ((c>>10) & 0x1f) * scaler;
    return b | (g<<5) | (r<<10);
}

void traverse_brick(Ray* ray, uint64_t brick_index, uint32_t scale) {

    float init_ray_pos_x = ray->pos.x;
    float init_ray_pos_y = ray->pos.y;
    float init_ray_pos_z = ray->pos.z;

    uint32_t init_x = (uint32_t)ray->pos.x & (-scale);
    uint32_t init_y = (uint32_t)ray->pos.y & (-scale);
    uint32_t init_z = (uint32_t)ray->pos.z & (-scale);
    if (ray->inv_dir_x < 0 && init_x != ray->pos.x) init_x += scale;
    if (ray->inv_dir_y < 0 && init_y != ray->pos.y) init_y += scale;
    if (ray->inv_dir_z < 0 && init_z != ray->pos.z) init_z += scale;

    uint32_t x = init_x;
    uint32_t y = init_y;
    uint32_t z = init_z;

    uint32_t brick_coord_x = (((ray->inv_dir_x > 0) ? x : (x - scale))/scale)&3;
    uint32_t brick_coord_y = (((ray->inv_dir_y > 0) ? y : (y - scale))/scale)&3;
    uint32_t brick_coord_z = (((ray->inv_dir_z > 0) ? z : (z - scale))/scale)&3;

    int32_t step_x = scale*sign(ray->inv_dir_x);
    int32_t step_y = scale*sign(ray->inv_dir_y);
    int32_t step_z = scale*sign(ray->inv_dir_z);

    float t_step_x = step_x*ray->inv_dir_x;
    float t_step_y = step_y*ray->inv_dir_y;
    float t_step_z = step_z*ray->inv_dir_z;

    float prev_t_x = 0;
    float prev_t_y = 0;
    float prev_t_z = 0;

    float t_x = (x + step_x - ray->pos.x)*ray->inv_dir_x;
    float t_y = (y + step_y - ray->pos.y)*ray->inv_dir_y;
    float t_z = (z + step_z - ray->pos.z)*ray->inv_dir_z;

    int8_t last_step = -1;

    for (;;) {
        if (brick_array[brick_index].occupancy & ((uint64_t)1 << (brick_coord_x + (brick_coord_y<<2) + (brick_coord_z<<4)))) {
            switch(last_step) {
                case 0:
                    ray->pos.x = x;
                    ray->pos.y = init_ray_pos_y + prev_t_x*ray->dir_y;
                    ray->pos.z = init_ray_pos_z + prev_t_x*ray->dir_z;
                    break;
                case 1:
                    ray->pos.x = init_ray_pos_x + prev_t_y*ray->dir_x;
                    ray->pos.y = y;
                    ray->pos.z = init_ray_pos_z + prev_t_y*ray->dir_z;
                    break;
                case 2:
                    ray->pos.x = init_ray_pos_x + prev_t_z*ray->dir_x;
                    ray->pos.y = init_ray_pos_y + prev_t_z*ray->dir_y;
                    ray->pos.z = z;
                    break;
            }
            if (scale == 1) {
                ray->collisions++;
                ray->colour += voxel_array[brick_array[brick_index].voxel_array_index + brick_coord_x + (brick_coord_y<<2) + (brick_coord_z<<4)].colour;
                ray->colour = scale_colour(ray->colour, colour_from_ray(*ray));
                return;
            } else {
                traverse_brick(ray, brick_array[brick_index].brick_array_index + brick_coord_x + (brick_coord_y<<2) + (brick_coord_z<<4), scale>>2);
                if (ray->collisions >= 1) return;
            }
        }

        if (t_x <= t_y) {
            if (t_x <= t_z) {
                last_step = 0;
                x += step_x;
                brick_coord_x += sign(ray->inv_dir_x);
                prev_t_x = t_x;
                t_x += t_step_x;
            } else {
                last_step = 2;
                z += step_z;
                brick_coord_z += sign(ray->inv_dir_z);
                prev_t_z = t_z;
                t_z += t_step_z;
            }
        } else {
            if (t_y <= t_z) {
                last_step = 1;
                y += step_y;
                brick_coord_y += sign(ray->inv_dir_y);
                prev_t_y = t_y;
                t_y += t_step_y;
            } else {
                last_step = 2;
                z += step_z;
                brick_coord_z += sign(ray->inv_dir_z);
                prev_t_z = t_z;
                t_z += t_step_z;
            }
        }

        if (((x & (3*scale)) == 0 && x != init_x)
            || ((y & (3*scale)) == 0 && y != init_y)
            || ((z & (3*scale)) == 0 && z != init_z)) {
            if (scale == WORLD_SCALE>>2) {
                ray->collisions++;
                if (ray->collisions == 1)
                    ray->colour = SKY;
            }
            return;
        }
    }
}

int out_of_world(Ray* ray) {
    return (ray->pos.x > (WORLD_SCALE)) || (ray->pos.x < 0)
        || (ray->pos.x == (WORLD_SCALE) && ray->dir_x > 0) || (ray->pos.x == 0 && ray->dir_x < 0)
        || (ray->pos.y > (WORLD_SCALE)) || (ray->pos.y < 0)
        || (ray->pos.y == (WORLD_SCALE) && ray->dir_y > 0) || (ray->pos.y == 0 && ray->dir_y < 0)
        || (ray->pos.z > (WORLD_SCALE)) || (ray->pos.z < 0)
        || (ray->pos.z == (WORLD_SCALE) && ray->dir_z > 0) || (ray->pos.z == 0 && ray->dir_z < 0);
}

uint16_t render_pixel(int screen_x, int screen_y, Camera c) {

    Ray ray = start_rays[screen_x + screen_y*GAME_RES_WIDTH];

    if (out_of_world(&ray)) {
        return SKY;
    }

    traverse_brick(&ray, 0, WORLD_SCALE>>2);

    return ray.colour;
}

void render_world(Camera c) {
    for (int y = 0; y < GAME_RES_HEIGHT; y++) {
        for (int x = 0; x < GAME_RES_WIDTH; x++) {
            pixel(x, y, render_pixel(x, y, c));
        }
    }
}

void render(HDC hdc) {
    render_world(camera);
    StretchDIBits(hdc, 0, 0, windowWidth, windowHeight, 0, 0, GAME_RES_WIDTH, GAME_RES_HEIGHT, buffer.memory, &buffer.bitmapInfo, DIB_RGB_COLORS, SRCCOPY);
}

int16_t get_voxel_colour(uint32_t x, uint32_t y, uint32_t z) {
    uint32_t height;
    if (height_map[x + z*WORLD_SCALE] == -1) {
        height = (int)(((float)WORLD_SCALE/16)*sin(x*16/(float)WORLD_SCALE) + ((float)WORLD_SCALE/16)*sin(z*16/(float)WORLD_SCALE) + (float)WORLD_SCALE/8);
        height_map[x + z*WORLD_SCALE] = height;
    } else
        height = height_map[x + z*WORLD_SCALE];

    if (y <= height) return (x&0x1f) + ((y&0x1f)<<5) + ((z&0x1f)<<10);
    return -1;
}

uint64_t create_brick(uint32_t scale, size_t brick_index, size_t* next_brick_index, size_t* next_voxel_index, uint32_t pos_x, uint32_t pos_y, uint32_t pos_z) {
    if (scale == 1) {
        brick_array[brick_index].voxel_array_index = *next_voxel_index;
        *next_voxel_index += 64;
    } else {
        brick_array[brick_index].brick_array_index = *next_brick_index;
        *next_brick_index += 64;
    }

    brick_array[brick_index].occupancy = 0;

    for (uint8_t z = 0; z < 4; z++) {
        for (uint8_t y = 0; y < 4; y++) {
            for (uint8_t x = 0; x < 4; x++) {
                if (scale == 1) {
                    int16_t c = get_voxel_colour((pos_x<<2)|x, (pos_y<<2)|y, (pos_z<<2)|z);
                    if (c == -1) {
                        voxel_array[brick_array[brick_index].voxel_array_index + x + (y<<2) + (z<<4)] = (Voxel){0, 0};
                    } else {
                        voxel_array[brick_array[brick_index].voxel_array_index + x + (y<<2) + (z<<4)] = (Voxel){c, 1};
                        brick_array[brick_index].occupancy |= (uint64_t)1<<(x + (y<<2) + (z<<4));
                    }
                } else {
                    if (create_brick(scale >> 2, brick_array[brick_index].brick_array_index + x + (y<<2) + (z<<4), next_brick_index, next_voxel_index, (pos_x<<2)|x, (pos_y<<2)|y, (pos_z<<2)|z)) {
                        brick_array[brick_index].occupancy |= (uint64_t)1<<(x + (y<<2) + (z<<4));
                    }
                }
            }
        }
    }
    if (brick_array[brick_index].occupancy == 0) {
        if (scale == 1)
            *next_voxel_index -= 64;
        else
            *next_brick_index -= 64;
    }
    return brick_array[brick_index].occupancy;
}

void create_world() {

    for (int i = 0; i < WORLD_SCALE*WORLD_SCALE; i++) {
        height_map[i] = -1;
    }

    size_t brick_array_index = 1;
    size_t voxel_array_index = 0;
    brick_array = malloc(((size_t)WORLD_SCALE*WORLD_SCALE*WORLD_SCALE - 1)/7 * sizeof(Brick));
    if (!brick_array) {
        printf("brick_array required too  much memory\n");
        exit(0);
    }
    voxel_array = malloc((size_t)WORLD_SCALE*WORLD_SCALE*WORLD_SCALE*sizeof(Voxel));
    if (!voxel_array) {
        printf("voxel_array required too  much memory\n");
        exit(0);
    }
    create_brick(WORLD_SCALE>>2, 0, &brick_array_index, &voxel_array_index, 0, 0, 0);
}


void fill_ray_pos_arrays() {
    for (int x = 0; x < GAME_RES_WIDTH; x++) {
        ray_x_pos_array[x] = (float)(x - GAME_RES_WIDTH/2.0)/SCREEN_SCALING_FACTOR;
    }
    for (int y = 0; y < GAME_RES_HEIGHT; y++) {
        ray_y_pos_array[y] = (float)(y - GAME_RES_HEIGHT/2.0)/SCREEN_SCALING_FACTOR;
    }
}

int APIENTRY WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, PSTR CmdLine, int CmdShow) {
    fill_ray_pos_arrays();
    create_world();

    camera = (Camera){(Point3){WORLD_SCALE>>1, WORLD_SCALE>>1, WORLD_SCALE>>1}, 0, 0, 0, 0, 0, 0, 2.0944, 0};
    camera.cos_pitch = cos(camera.pitch);
    camera.sin_pitch = sin(camera.pitch);
    camera.cos_yaw = cos(camera.yaw);
    camera.sin_yaw = sin(camera.yaw);
    camera.focal_point = GAME_RES_WIDTH/2.0/tan(camera.fov/2.0)/SCREEN_SCALING_FACTOR;

    update_start_rays(1, 1, 1);

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

    windowRect = (RECT){MONITOR_CENTER_H-INITIAL_WINDOW_WIDTH/2, MONITOR_CENTER_V-INITIAL_WINDOW_HEIGHT/2, MONITOR_CENTER_H+INITIAL_WINDOW_WIDTH/2, MONITOR_CENTER_V+INITIAL_WINDOW_HEIGHT/2};

    if (!AdjustWindowRectEx(&windowRect, WS_OVERLAPPEDWINDOW, FALSE, 0)) {
        MessageBox(NULL, "Adjust window size failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 1;
    }

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

    windowHandle = CreateWindowEx(0, WindowClass.lpszClassName, "Window Title", WS_OVERLAPPEDWINDOW | WS_VISIBLE, windowRect.left, windowRect.top, windowRect.right - windowRect.left, windowRect.bottom - windowRect.top, NULL, NULL, Instance, NULL);

    if (windowHandle == NULL) {
        MessageBox(NULL, "Window Creation Failed!", "Error!", MB_ICONEXCLAMATION | MB_OK);
        return 0;
    }

    UpdateWindow(windowHandle);

    hdc = GetDC(windowHandle);

    running = 1;
    MSG Msg;

    float timer = (float)clock() / CLOCKS_PER_SEC;
    int frames = 0;

    float last_time = (float)clock() / CLOCKS_PER_SEC;
    float ups = 20;
    float fraction_of_update = 0;
    int updates = 0;

    char window_name[30];

    light_height = WORLD_SCALE;

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

        float pre_render = (float)clock() / CLOCKS_PER_SEC * 1000.f;
        render(hdc);
        float post_render = (float)clock() / CLOCKS_PER_SEC * 1000.f;
        int mspframe = post_render - pre_render;
        frames++;

        if ((float)clock()/CLOCKS_PER_SEC - timer > 1.0) {
            timer++;
            sprintf(window_name, "%dms | %dfps | %dtps", mspframe, frames, updates);
            SetWindowTextA(windowHandle, window_name);
            frames = 0;
            updates = 0;
        }
    }

    ReleaseDC(windowHandle, hdc);

    return 0;
}
