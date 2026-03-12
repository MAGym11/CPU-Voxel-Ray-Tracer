#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <windows.h>
#include <math.h>
#include <time.h>
#include <wingdi.h>

#define INITIAL_WINDOW_WIDTH 768
#define INITIAL_WINDOW_HEIGHT 432

#define GAME_RES_WIDTH  384
#define GAME_RES_HEIGHT 216
#define GAME_BPP        32
#define GAME_BITMAP_MEM_SIZE (GAME_RES_WIDTH * GAME_RES_HEIGHT * (GAME_BPP / 8))

#define MONITOR_CENTER_H (monitorinfo.rcMonitor.left + monitorinfo.rcMonitor.right)/2
#define MONITOR_CENTER_V (monitorinfo.rcMonitor.top + monitorinfo.rcMonitor.bottom)/2

#define WORLD_SCALE 256

#define SCREEN_SCALING_FACTOR (GAME_RES_WIDTH/2.0)

#define pixel(x, y, colour) *((int*)buffer.memory + x + y*GAME_RES_WIDTH) = colour

#define SKY 0x87CEEB

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

typedef struct {
    uint8_t r, g, b;
} Colour;

typedef struct Voxel {
    Colour colour;
    uint8_t occupancy;
} Voxel;

typedef struct Octree {
    union {
        uint32_t octant_array_index;
        uint32_t voxel_array_index;
    };
    uint8_t occupancy;
} Octree;

typedef struct {
    Point3 pos;
    float dir_x, dir_y, dir_z, inv_dir_x, inv_dir_y, inv_dir_z;
    int collisions;
    Colour colour;
} Ray;

Camera camera;

Octree* octant_array;
Voxel* voxel_array;

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

Point3 world_space_translation;

float light_height;

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
                    windowWidth = windowRect.right - windowRect.left;
                    windowHeight = windowRect.bottom - windowRect.top;
                    if (lastSizeMsg) {
                        SetWindowPos(windowHandle, HWND_TOP, windowRect.left, windowRect.top, windowWidth, windowHeight, SWP_FRAMECHANGED);
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
                start_rays[x + y*GAME_RES_WIDTH].colour = (Colour){0,0,0};
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


    light_height -= WORLD_SCALE*0.01f;
    if (light_height <= 0) light_height += WORLD_SCALE*2.0f;

    float v = WORLD_SCALE*0.025f;

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

    if (camera.pos.x > WORLD_SCALE << 1) camera.pos.x = WORLD_SCALE << 1;
    if (camera.pos.x < 0) camera.pos.x = 0;
    if (camera.pos.y > WORLD_SCALE << 1) camera.pos.y = WORLD_SCALE << 1;
    if (camera.pos.y < 0) camera.pos.y = 0;
    if (camera.pos.z > WORLD_SCALE << 1) camera.pos.z = WORLD_SCALE << 1;
    if (camera.pos.z < 0) camera.pos.z = 0;

    if (camera_moved_xz
            || camera_moved_y
            || camera_rotated) update_start_rays(camera_moved_xz, camera_moved_y, camera_rotated);
}

void advance_ray(Ray* ray, int scale) {

    float octree_space_ray_x = (int)ray->pos.x & (-scale);
    float octree_space_ray_y = (int)ray->pos.y & (-scale);
    float octree_space_ray_z = (int)ray->pos.z & (-scale);

    float dx = ray->pos.x - octree_space_ray_x;
    float dy = ray->pos.y - octree_space_ray_y;
    float dz = ray->pos.z - octree_space_ray_z;

    if (!signbit(ray->dir_x)) dx -= scale;
    else if (ray->pos.x == octree_space_ray_x) dx += scale;
    if (!signbit(ray->dir_y)) dy -= scale;
    else if (ray->pos.y == octree_space_ray_y) dy += scale;
    if (!signbit(ray->dir_z)) dz -= scale;
    else if (ray->pos.z == octree_space_ray_z) dz += scale;

    float t_x = fabsf(dx * ray->inv_dir_x);
    float t_y = fabsf(dy * ray->inv_dir_y);
    float t_z = fabsf(dz * ray->inv_dir_z);

    float min_t = t_x;
    char min_coord = 0;

    if (t_y < t_x) {
        min_t = t_y;
        min_coord = 1;
    }
    if (t_z < min_t) {
        min_t = t_z;
        min_coord = 2;
    }

    switch (min_coord) {
        case 0:
            ray->pos.x -= dx;
            ray->pos.x = roundf(ray->pos.x);
            ray->pos.y += ray->dir_y * min_t;
            ray->pos.z += ray->dir_z * min_t;
            break;
        case 1:
            ray->pos.x += ray->dir_x * min_t;
            ray->pos.y -= dy;
            ray->pos.y = roundf(ray->pos.y);
            ray->pos.z += ray->dir_z * min_t;
            break;
        default:
            ray->pos.x += ray->dir_x * min_t;
            ray->pos.y += ray->dir_y * min_t;
            ray->pos.z -= dz;
            ray->pos.z = roundf(ray->pos.z);
    }
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
                        + (WORLD_SCALE - ray.pos.y) * normal.y
                        + (light_height - ray.pos.z) * normal.z;
    dot_product *= inv_modulus((light_height - ray.pos.x),
                                (WORLD_SCALE - ray.pos.y),
                                (light_height - ray.pos.z));
    return dot_product * 0.5 + 0.5;
}

extern inline Colour scale_Colour(Colour c, float scaler) {
    c.r = c.r * scaler;
    c.g = c.g * scaler;
    c.b = c.b * scaler;
    return c;
}

extern inline Colour to_Colour(int colour) {
    return (Colour){(colour & 0xff0000)>>16, (colour & 0xff00)>>8, colour & 0xff};
}

extern inline uint8_t find_octant(Ray* ray, uint32_t scale) {

    int x_pos = ray->pos.x;
    int y_pos = ray->pos.y;
    int z_pos = ray->pos.z;

    if (x_pos == ray->pos.x && ray->dir_x < 0) x_pos--;
    if (y_pos == ray->pos.y && ray->dir_y < 0) y_pos--;
    if (z_pos == ray->pos.z && ray->dir_z < 0) z_pos--;

    uint8_t octant = 0;
    if (x_pos & scale) octant += 1;
    if (y_pos & scale) octant += 2;
    if (z_pos & scale) octant += 4;

    return octant;
}

extern inline int out_of_world(Ray* ray) {
    return (ray->pos.x > (WORLD_SCALE << 1)) || (ray->pos.x < 0)
        || (ray->pos.x == (WORLD_SCALE << 1) && ray->dir_x > 0) || (ray->pos.x == 0 && ray->dir_x < 0)
        || (ray->pos.y > (WORLD_SCALE << 1)) || (ray->pos.y < 0)
        || (ray->pos.y == (WORLD_SCALE << 1) && ray->dir_y > 0) || (ray->pos.y == 0 && ray->dir_y < 0)
        || (ray->pos.z > (WORLD_SCALE << 1)) || (ray->pos.z < 0)
        || (ray->pos.z == (WORLD_SCALE << 1) && ray->dir_z > 0) || (ray->pos.z == 0 && ray->dir_z < 0);
}

extern inline int out_of_bounds(Ray* ray, int scale) {
    int pos_x = (int)ray->pos.x;
    int pos_y = (int)ray->pos.y;
    int pos_z = (int)ray->pos.z;
    
    if (ray->pos.x == (pos_x & (-(scale<<1)))
        || ray->pos.y == (pos_y & (-(scale<<1)))
        || ray->pos.z == (pos_z & (-(scale<<1)))) return 1;

    return 0;
}

void traverse_node(Ray* ray, uint32_t octant_index, uint32_t scale) {
    if (out_of_world(ray)) {
        ray->collisions++;
        if (ray->collisions == 1)
            ray->colour = to_Colour(SKY);
        return;
    }

    while (1) {
        uint8_t octant = find_octant(ray, scale);

        if (octant_array[octant_index].occupancy & (1 << octant)) {
            if (scale == 1) {
                ray->collisions++;
                if (ray->collisions == 1) {
                    ray->colour.r += voxel_array[octant_array[octant_index].voxel_array_index + octant].colour.r;
                    ray->colour.g += voxel_array[octant_array[octant_index].voxel_array_index + octant].colour.g;
                    ray->colour.b += voxel_array[octant_array[octant_index].voxel_array_index + octant].colour.b;
                    ray->colour = scale_Colour(ray->colour, colour_from_ray(*ray));
                    ray->dir_x = light_height - ray->pos.x;
                    ray->dir_y = WORLD_SCALE - ray->pos.y;
                    ray->dir_z = light_height - ray->pos.z;
                    ray->inv_dir_x = 1.0f / ray->dir_x;
                    ray->inv_dir_y = 1.0f / ray->dir_y;
                    ray->inv_dir_z = 1.0f / ray->dir_z;
                    goto check_out_of_bounds;
                } else if (ray->collisions == 2) {
                    ray->colour = scale_Colour(ray->colour, 0.5);
                    return;
                }
            }
            traverse_node(ray, octant_array[octant_index].octant_array_index + octant, scale>>1);
            if (ray->collisions < 2) goto check_out_of_bounds;
            return;
        }
        advance_ray(ray, scale);
check_out_of_bounds:
        if (out_of_world(ray)) {
            ray->collisions++;
            if (ray->collisions == 1)
                ray->colour = to_Colour(SKY);
            return;
        }
        if (out_of_bounds(ray, scale)) return;
    }
}

int render_pixel(int screen_x, int screen_y, Camera c) {

    Ray ray = start_rays[screen_x + screen_y*GAME_RES_WIDTH];
    world_space_translation = (Point3){0,0,0};
    traverse_node(&ray, 0, WORLD_SCALE);

    return ((ray.colour.r & 0xff) << 16) | ((ray.colour.g & 0xff) << 8) | (ray.colour.b & 0xff);
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

int get_voxel_colour(uint32_t x, uint32_t y, uint32_t z) {
    uint32_t height = (int)(((float)WORLD_SCALE/8)*sin(x*8/(float)WORLD_SCALE) + ((float)WORLD_SCALE/8)*sin(z*8/(float)WORLD_SCALE) + (float)WORLD_SCALE/4);
    if (y <= height) return 0xffffff;
    return -1;
}

uint8_t create_octree(uint32_t scale, uint32_t octant_index, uint32_t* next_octant_index, uint32_t* next_voxel_index, uint32_t pos_x, uint32_t pos_y, uint32_t pos_z) {
    if (scale == 1) {
        octant_array[octant_index].voxel_array_index = *next_voxel_index;
        *next_voxel_index += 8;
    } else {
        octant_array[octant_index].octant_array_index = *next_octant_index;
        *next_octant_index += 8;
    }

    octant_array[octant_index].occupancy = 0;

    for (int z = 0; z < 2; z++) {
        for (int y = 0; y < 2; y++) {
            for (int x = 0; x < 2; x++) {
                if (scale == 1) {
                    int c = get_voxel_colour((pos_x<<1)|x, (pos_y<<1)|y, (pos_z<<1)|z);
                    if (c == -1) {
                        voxel_array[octant_array[octant_index].voxel_array_index + x + (y<<1) + (z<<2)] = (Voxel){to_Colour(0), 0};
                    } else {
                        voxel_array[octant_array[octant_index].voxel_array_index + x + (y<<1) + (z<<2)] = (Voxel){to_Colour(c), 1};
                        octant_array[octant_index].occupancy |= 1<<(x + (y<<1) + (z<<2));
                    }
                } else {
                    if (create_octree(scale >> 1, octant_array[octant_index].octant_array_index + x + (y<<1) + (z<<2), next_octant_index, next_voxel_index, (pos_x<<1)|x, (pos_y<<1)|y, (pos_z<<1)|z)) {
                        octant_array[octant_index].occupancy |= 1<<(x + (y<<1) + (z<<2));
                    }
                }
            }
        }
    }
    return octant_array[octant_index].occupancy;
}

void create_world() {
    uint32_t octant_array_index = 1;
    uint32_t voxel_array_index = 0;
    octant_array = malloc((8*WORLD_SCALE*WORLD_SCALE*WORLD_SCALE - 1)/7 * sizeof(Octree));
    voxel_array = malloc(8*WORLD_SCALE*WORLD_SCALE*WORLD_SCALE*sizeof(Voxel));
    create_octree(WORLD_SCALE, 0, &octant_array_index, &voxel_array_index, 0, 0, 0);
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

    camera = (Camera){(Point3){WORLD_SCALE, WORLD_SCALE, WORLD_SCALE}, 0, 0, 0, 0, 0, 0, 2.0944, 0};
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

    light_height = WORLD_SCALE<<1;

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
