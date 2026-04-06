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

#define GAME_RES_WIDTH  640
#define GAME_RES_HEIGHT 360
#define GAME_BPP        32
#define GAME_BITMAP_MEM_SIZE (GAME_RES_WIDTH * GAME_RES_HEIGHT * (GAME_BPP / 8))

#define MONITOR_CENTER_H (monitorinfo.rcMonitor.left + monitorinfo.rcMonitor.right)/2
#define MONITOR_CENTER_V (monitorinfo.rcMonitor.top + monitorinfo.rcMonitor.bottom)/2

#define WORLD_SCALE     1024
#define LOG_WORLD_SCALE 10

#define SCREEN_SCALING_FACTOR (GAME_RES_WIDTH/2.0)

#define pixel(x, y, colour) *((uint32_t*)buffer.memory + x + y*GAME_RES_WIDTH) = colour

#define SKY 0x87CEEB

int windowWidth = INITIAL_WINDOW_WIDTH, windowHeight = INITIAL_WINDOW_HEIGHT;
int monitorWidth, monitorHeight;

typedef struct Bitmap {
    BITMAPINFO bitmapInfo;
    void* memory;
} Bitmap;

typedef struct {
    float x, y, z;
} float3;

typedef struct {
    uint32_t x, y, z;
} uint32_t3;

typedef struct {
    int32_t x, y, z;
} int32_t3;

typedef struct {
    float3 pos;
    float pitch, yaw;
    float cos_pitch, sin_pitch;
    float cos_yaw, sin_yaw;
    float fov;
    float focal_point;
} Camera;

typedef struct Voxel {
    uint32_t colour;
} Voxel;

typedef struct Brick {
    union {
        uint64_t brick_array_index;
        uint64_t voxel_array_index;
    };
    uint64_t occupancy;
} Brick;

typedef struct {
    float3 pos;
    float3 dir;
    float3 inv_dir;
    int collisions;
    uint32_t colour;
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

int32_t height_map[WORLD_SCALE*WORLD_SCALE];

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

    new_ray.dir.x = ray.dir.x*c.cos_yaw - ray.dir.z*c.sin_yaw;
    new_ray.dir.y = ray.dir.y;
    new_ray.dir.z = ray.dir.z*c.cos_yaw + ray.dir.x*c.sin_yaw;

    new_ray.inv_dir.x = 1.0f / new_ray.dir.x;
    new_ray.inv_dir.y = 1.0f / new_ray.dir.y;
    new_ray.inv_dir.z = 1.0f / new_ray.dir.z;

    return new_ray;
}

extern inline Ray rotate_pitch(Ray ray, Camera c) {
    Ray new_ray;

    new_ray.pos.x = ray.pos.x;
    new_ray.pos.y = ray.pos.y*c.cos_pitch;
    new_ray.pos.z = ray.pos.y*c.sin_pitch;

    new_ray.dir.x = ray.dir.x;
    new_ray.dir.y = new_ray.pos.y - ray.dir.z*c.sin_pitch;
    new_ray.dir.z = ray.dir.z*c.cos_pitch + new_ray.pos.z;

    return new_ray;
}

void update_start_rays(char camera_moved_xz, char camera_moved_y, char camera_rotated) {
    for (int y = 0; y < GAME_RES_HEIGHT; y++) {
        for (int x = 0; x < GAME_RES_WIDTH; x++) {
            if (camera_rotated) {
                start_rays[x + y*GAME_RES_WIDTH].pos.x = ray_x_pos_array[x];
                start_rays[x + y*GAME_RES_WIDTH].pos.y = ray_y_pos_array[y];

                start_rays[x + y*GAME_RES_WIDTH].dir.x = ray_x_pos_array[x];
                start_rays[x + y*GAME_RES_WIDTH].dir.y = ray_y_pos_array[y];
                start_rays[x + y*GAME_RES_WIDTH].dir.z = -camera.focal_point;

                start_rays[x + y*GAME_RES_WIDTH] = rotate_pitch(start_rays[x + y*GAME_RES_WIDTH], camera);
                start_rays[x + y*GAME_RES_WIDTH] = rotate_yaw(start_rays[x + y*GAME_RES_WIDTH], camera);

                start_rays[x + y*GAME_RES_WIDTH].collisions = 0;
                start_rays[x + y*GAME_RES_WIDTH].colour = 0;
            }
            if (camera_moved_xz || camera_rotated) {
                start_rays[x + y*GAME_RES_WIDTH].pos.x = start_rays[x + y*GAME_RES_WIDTH].dir.x + camera.pos.x;
                start_rays[x + y*GAME_RES_WIDTH].pos.z = start_rays[x + y*GAME_RES_WIDTH].dir.z + camera.pos.z;
            }
            if (camera_moved_y || camera_rotated) {
                start_rays[x + y*GAME_RES_WIDTH].pos.y = start_rays[x + y*GAME_RES_WIDTH].dir.y + camera.pos.y;
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

float3 get_normal(Ray ray) {
    if (is_integer(ray.pos.x)) {
        return (ray.dir.x >= 0.0) ? (float3){-1, 0, 0} : (float3){1, 0, 0};
    } else if (is_integer(ray.pos.y)) {
        return (ray.dir.y >= 0.0) ? (float3){0, -1, 0} : (float3){0, 1, 0};
    } else if (is_integer(ray.pos.z)) {
        return (ray.dir.z >= 0.0) ? (float3){0, 0, -1} : (float3){0, 0, 1};
    } else {
        return (float3){0, 0, 0};
    }
}

extern inline float colour_from_ray(Ray ray) {
    float3 normal = get_normal(ray);
    float dot_product = (light_height - ray.pos.x) * normal.x
                        + ((WORLD_SCALE>>1) - ray.pos.y) * normal.y
                        + (light_height - ray.pos.z) * normal.z;
    dot_product *= inv_modulus((light_height - ray.pos.x),
                                ((WORLD_SCALE>>1) - ray.pos.y),
                                (light_height - ray.pos.z));
    return dot_product*0.5f + 0.5f;
}

extern inline uint32_t scale_colour(uint32_t c, float scaler) {
    uint8_t r = (c & 0xff) * scaler;
    uint8_t g = ((c>>8) & 0xff) * scaler;
    uint8_t b = ((c>>16) & 0xff) * scaler;
    return b | (g<<8) | (r<<16);
}

uint64_t potential_occupancy_mask(uint8_t sub_brick_index, float3 inv_dir) {

    uint64_t potential_occupancy_mask_x_pos[] = {0xffffffffffffffff, 0xeeeeeeeeeeeeeeee, 0xcccccccccccccccc, 0x8888888888888888};
    uint64_t potential_occupancy_mask_x_neg[] = {0x1111111111111111, 0x3333333333333333, 0x7777777777777777, 0xffffffffffffffff};
    uint64_t potential_occupancy_mask_y_pos[] = {0xffffffffffffffff, 0xfff0fff0fff0fff0, 0xff00ff00ff00ff00, 0xf000f000f000f000};
    uint64_t potential_occupancy_mask_y_neg[] = {0x000f000f000f000f, 0x00ff00ff00ff00ff, 0x0fff0fff0fff0fff, 0xffffffffffffffff};
    uint64_t potential_occupancy_mask_z_neg[] = {0x000000000000ffff, 0x00000000ffffffff, 0x0000ffffffffffff, 0xffffffffffffffff};
    uint64_t potential_occupancy_mask_z_pos[] = {0xffffffffffffffff, 0xffffffffffff0000, 0xffffffff00000000, 0xffff000000000000};

    uint64_t mask_x, mask_y, mask_z;

    if (inv_dir.x > 0)
        mask_x = potential_occupancy_mask_x_pos[sub_brick_index&3];
    else
        mask_x = potential_occupancy_mask_x_neg[sub_brick_index&3];

    if (inv_dir.y > 0)
        mask_y = potential_occupancy_mask_y_pos[(sub_brick_index>>2)&3];
    else
        mask_y = potential_occupancy_mask_y_neg[(sub_brick_index>>2)&3];

    if (inv_dir.z > 0)
        mask_z = potential_occupancy_mask_z_pos[(sub_brick_index>>4)&3];
    else
        mask_z = potential_occupancy_mask_z_neg[(sub_brick_index>>4)&3];

    return mask_x & mask_y & mask_z;
}

void traverse_brick(Ray* ray, uint64_t brick_index, uint32_t scale, uint8_t log_scale) {

    uint32_t init_x = (uint32_t)ray->pos.x & (-scale);
    uint32_t init_y = (uint32_t)ray->pos.y & (-scale);
    uint32_t init_z = (uint32_t)ray->pos.z & (-scale);

    if (ray->inv_dir.x < 0 && ray->pos.x != init_x) init_x += scale;
    if (ray->inv_dir.y < 0 && ray->pos.y != init_y) init_y += scale;
    if (ray->inv_dir.z < 0 && ray->pos.z != init_z) init_z += scale;

    float init_ray_pos_x = ray->pos.x;
    float init_ray_pos_y = ray->pos.y;
    float init_ray_pos_z = ray->pos.z;

    int32_t3 step;
    step.x = scale*sign(ray->inv_dir.x);
    step.y = scale*sign(ray->inv_dir.y);
    step.z = scale*sign(ray->inv_dir.z);

    float3 t_step;
    t_step.x = step.x*ray->inv_dir.x;
    t_step.y = step.y*ray->inv_dir.y;
    t_step.z = step.z*ray->inv_dir.z;

    float3 curr_t = {0,0,0};

    uint32_t x = (uint32_t)ray->pos.x & (-scale);
    uint32_t y = (uint32_t)ray->pos.y & (-scale);
    uint32_t z = (uint32_t)ray->pos.z & (-scale);
    if (ray->inv_dir.x < 0 && ray->pos.x == x) x -= scale;
    if (ray->inv_dir.y < 0 && ray->pos.y == y) y -= scale;
    if (ray->inv_dir.z < 0 && ray->pos.z == z) z -= scale;

    float3 next_t;
    next_t.x = (init_x + step.x - ray->pos.x)*ray->inv_dir.x;
    next_t.y = (init_y + step.y - ray->pos.y)*ray->inv_dir.y;
    next_t.z = (init_z + step.z - ray->pos.z)*ray->inv_dir.z;

    int8_t last_step = -1;

    int8_t sub_brick_index_step_x = sign(ray->inv_dir.x);
    int8_t sub_brick_index_step_y = 4*sign(ray->inv_dir.y);
    int8_t sub_brick_index_step_z = 16*sign(ray->inv_dir.z);

    uint8_t sub_brick_index = ((x>>log_scale)&3) + (((y>>log_scale)&3)<<2) + (((z>>log_scale)&3)<<4);

    uint32_t3 edge_offset;
    edge_offset.x = (ray->inv_dir.x < 0) ? scale : 0;
    edge_offset.y = (ray->inv_dir.y < 0) ? scale : 0;
    edge_offset.z = (ray->inv_dir.z < 0) ? scale : 0;

    for (;;) {
        if (brick_array[brick_index].occupancy & ((uint64_t)1 << sub_brick_index)) {
            switch(last_step) {
                case 0:
                    ray->pos.x = x + edge_offset.x;
                    ray->pos.y = init_ray_pos_y + curr_t.x*ray->dir.y;
                    ray->pos.z = init_ray_pos_z + curr_t.x*ray->dir.z;
                    break;
                case 1:
                    ray->pos.x = init_ray_pos_x + curr_t.y*ray->dir.x;
                    ray->pos.y = y + edge_offset.y;
                    ray->pos.z = init_ray_pos_z + curr_t.y*ray->dir.z;
                    break;
                case 2:
                    ray->pos.x = init_ray_pos_x + curr_t.z*ray->dir.x;
                    ray->pos.y = init_ray_pos_y + curr_t.z*ray->dir.y;
                    ray->pos.z = z + edge_offset.z;
                    break;
            }
            if (scale == 1) {
                ray->collisions++;
                ray->colour += voxel_array[brick_array[brick_index].voxel_array_index + sub_brick_index].colour;
                return;
                ray->colour = scale_colour(ray->colour, colour_from_ray(*ray));
            } else {
                traverse_brick(ray, brick_array[brick_index].brick_array_index + sub_brick_index, scale>>2, log_scale-2);
                if (ray->collisions >= 1) return;
            }
        }

        if (next_t.x <= next_t.y && next_t.x <= next_t.z) {
            last_step = 0;
            x += step.x;
            if (((x + edge_offset.x) & (3*scale)) == 0) return;
            sub_brick_index += sub_brick_index_step_x;
            curr_t.x = next_t.x;
            next_t.x += t_step.x;
        } else if (next_t.y <= next_t.z) {
            last_step = 1;
            y += step.y;
            if (((y + edge_offset.y) & (3*scale)) == 0) return;
            sub_brick_index += sub_brick_index_step_y;
            curr_t.y = next_t.y;
            next_t.y += t_step.y;
        } else {
            last_step = 2;
            z += step.z;
            if (((z + edge_offset.z) & (3*scale)) == 0) return;
            sub_brick_index += sub_brick_index_step_z;
            curr_t.z = next_t.z;
            next_t.z += t_step.z;
        }
    }
}

int advance_ray_into_world(Ray* ray) {
    float3 edge_0_t;
    edge_0_t.x = -ray->pos.x * ray->inv_dir.x;
    edge_0_t.y = -ray->pos.y * ray->inv_dir.y;
    edge_0_t.z = -ray->pos.z * ray->inv_dir.z;

    float3 edge_1_t;
    edge_1_t.x = (WORLD_SCALE-ray->pos.x)*ray->inv_dir.x;
    edge_1_t.y = (WORLD_SCALE-ray->pos.y)*ray->inv_dir.y;
    edge_1_t.z = (WORLD_SCALE-ray->pos.z)*ray->inv_dir.z;

    float3 min_t;
    float3 max_t;

    min_t.x = (edge_0_t.x < edge_1_t.x) ? edge_0_t.x : edge_1_t.x;
    min_t.y = (edge_0_t.y < edge_1_t.y) ? edge_0_t.y : edge_1_t.y;
    min_t.z = (edge_0_t.z < edge_1_t.z) ? edge_0_t.z : edge_1_t.z;

    max_t.x = (edge_0_t.x > edge_1_t.x) ? edge_0_t.x : edge_1_t.x;
    max_t.y = (edge_0_t.y > edge_1_t.y) ? edge_0_t.y : edge_1_t.y;
    max_t.z = (edge_0_t.z > edge_1_t.z) ? edge_0_t.z : edge_1_t.z;

    float t_first_edge = fmaxf(fmaxf(min_t.x, min_t.y), min_t.z);
    float t_second_edge = fminf(fminf(max_t.x, max_t.y), max_t.z);

    if (t_first_edge > t_second_edge) return 0;

    ray->pos.x += (t_first_edge + 0.001) * ray->dir.x;
    ray->pos.y += (t_first_edge + 0.001) * ray->dir.y;
    ray->pos.z += (t_first_edge + 0.001) * ray->dir.z;

    return 1;
}

int out_of_world(Ray* ray) {
    return (ray->pos.x > (WORLD_SCALE)) || (ray->pos.x < 0)
        || (ray->pos.x == (WORLD_SCALE) && ray->dir.x > 0) || (ray->pos.x == 0 && ray->dir.x < 0)
        || (ray->pos.y > (WORLD_SCALE)) || (ray->pos.y < 0)
        || (ray->pos.y == (WORLD_SCALE) && ray->dir.y > 0) || (ray->pos.y == 0 && ray->dir.y < 0)
        || (ray->pos.z > (WORLD_SCALE)) || (ray->pos.z < 0)
        || (ray->pos.z == (WORLD_SCALE) && ray->dir.z > 0) || (ray->pos.z == 0 && ray->dir.z < 0);
}

uint32_t render_pixel(int screen_x, int screen_y, Camera c) {

    Ray ray = start_rays[screen_x + screen_y*GAME_RES_WIDTH];

    if (out_of_world(&ray) && !advance_ray_into_world(&ray)) {
        return SKY;
    }

    traverse_brick(&ray, 0, WORLD_SCALE>>2, LOG_WORLD_SCALE-2);

    if (ray.collisions == 0) return SKY;

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

int32_t get_voxel_colour(uint32_t x, uint32_t y, uint32_t z) {
    uint32_t height;
    if (height_map[x + z*WORLD_SCALE] == -1) {
        height = (int)(((float)WORLD_SCALE/16)*sin(x*16/(float)WORLD_SCALE) + ((float)WORLD_SCALE/16)*sin(z*16/(float)WORLD_SCALE) + (float)WORLD_SCALE/8);
        height_map[x + z*WORLD_SCALE] = height;
    } else
        height = height_map[x + z*WORLD_SCALE];

    if (y <= height) return (x&0xff) + ((y&0xff)<<8) + ((z&0xff)<<16);
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
                    int32_t c = get_voxel_colour((pos_x<<2)|x, (pos_y<<2)|y, (pos_z<<2)|z);
                    if (c == -1) {
                        voxel_array[brick_array[brick_index].voxel_array_index + x + (y<<2) + (z<<4)] = (Voxel){0};
                    } else {
                        voxel_array[brick_array[brick_index].voxel_array_index + x + (y<<2) + (z<<4)] = (Voxel){c};
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
        printf("brick_array required too much memory\n");
        exit(0);
    }
    voxel_array = malloc((size_t)WORLD_SCALE*WORLD_SCALE*WORLD_SCALE*sizeof(Voxel));
    if (!voxel_array) {
        printf("voxel_array required too much memory\n");
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

    camera = (Camera){(float3){WORLD_SCALE>>1, WORLD_SCALE>>1, WORLD_SCALE>>1}, 0, 0, 0, 0, 0, 0, 2.0944, 0};
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
