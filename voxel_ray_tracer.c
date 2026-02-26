#include <stdio.h>
#include <windows.h>
#include <math.h>
#include <time.h>

#define INITIAL_WINDOW_WIDTH 768
#define INITIAL_WINDOW_HEIGHT 432

#define GAME_RES_WIDTH  384
#define GAME_RES_HEIGHT 216
#define GAME_BPP        32
#define GAME_BITMAP_MEM_SIZE (GAME_RES_WIDTH * GAME_RES_HEIGHT * (GAME_BPP / 8))

#define MONITOR_CENTER_H (monitorinfo.rcMonitor.left + monitorinfo.rcMonitor.right)/2
#define MONITOR_CENTER_V (monitorinfo.rcMonitor.top + monitorinfo.rcMonitor.bottom)/2

#define WORLD_SIZE 32 // max 1024

#define SCREEN_SCALING_FACTOR (GAME_RES_WIDTH/2.0)

#define pixel(x, y, colour) if (0 <= x && x < GAME_RES_WIDTH && 0 <= y && y < GAME_RES_HEIGHT) *((int*)buffer.memory + x + y*GAME_RES_WIDTH) = colour

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
    unsigned char r, g, b;
} Colour;

typedef struct Voxel {
    Colour colour;
    unsigned char occupancy;
} Voxel;

typedef struct Octree {
    union {
        unsigned int octant_array_index;
        unsigned int voxel_array_index;
    };
    Colour colour;
    unsigned char occupancy;
} Octree;

typedef struct {
    Point3 pos;
    float mx, my, mz, inv_mx, inv_my, inv_mz;
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

    new_ray.mx = ray.mx*c.cos_yaw - ray.mz*c.sin_yaw;
    new_ray.my = ray.my;
    new_ray.mz = ray.mz*c.cos_yaw + ray.mx*c.sin_yaw;

    new_ray.inv_mx = 1.0f / new_ray.mx;
    new_ray.inv_my = 1.0f / new_ray.my;
    new_ray.inv_mz = 1.0f / new_ray.mz;

    return new_ray;
}

extern inline Ray rotate_pitch(Ray ray, Camera c) {
    Ray new_ray;

    new_ray.pos.x = ray.pos.x;
    new_ray.pos.y = ray.pos.y*c.cos_pitch;
    new_ray.pos.z = ray.pos.y*c.sin_pitch;

    new_ray.mx = ray.mx;
    new_ray.my = new_ray.pos.y - ray.mz*c.sin_pitch;
    new_ray.mz = ray.mz*c.cos_pitch + new_ray.pos.z;

    return new_ray;
}

void update_start_rays(char camera_moved_xz, char camera_moved_y, char camera_rotated) {
    for (int y = 0; y < GAME_RES_HEIGHT; y++) {
        for (int x = 0; x < GAME_RES_WIDTH; x++) {
            if (camera_rotated) {
                start_rays[x + y*GAME_RES_WIDTH].pos.x = ray_x_pos_array[x];
                start_rays[x + y*GAME_RES_WIDTH].pos.y = ray_y_pos_array[y];

                start_rays[x + y*GAME_RES_WIDTH].mx = ray_x_pos_array[x];
                start_rays[x + y*GAME_RES_WIDTH].my = ray_y_pos_array[y];
                start_rays[x + y*GAME_RES_WIDTH].mz = -camera.focal_point;

                start_rays[x + y*GAME_RES_WIDTH] = rotate_pitch(start_rays[x + y*GAME_RES_WIDTH], camera);
                start_rays[x + y*GAME_RES_WIDTH] = rotate_yaw(start_rays[x + y*GAME_RES_WIDTH], camera);
            }
            if (camera_moved_xz || camera_rotated) {
                start_rays[x + y*GAME_RES_WIDTH].pos.x = start_rays[x + y*GAME_RES_WIDTH].mx + camera.pos.x;
                start_rays[x + y*GAME_RES_WIDTH].pos.z = start_rays[x + y*GAME_RES_WIDTH].mz + camera.pos.z;
            }
            if (camera_moved_y || camera_rotated) {
                start_rays[x + y*GAME_RES_WIDTH].pos.y = start_rays[x + y*GAME_RES_WIDTH].my + camera.pos.y;
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

    light_height -= 0.2f;
    if (light_height <= -WORLD_SIZE) light_height += WORLD_SIZE*2.0f;

    float v = 1.f;

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

    if (camera.pos.x > (float)WORLD_SIZE) camera.pos.x = (float)WORLD_SIZE;
    if (camera.pos.x < -(float)WORLD_SIZE) camera.pos.x = -(float)WORLD_SIZE;
    if (camera.pos.y > (float)WORLD_SIZE) camera.pos.y = (float)WORLD_SIZE;
    if (camera.pos.y < -(float)WORLD_SIZE) camera.pos.y = -(float)WORLD_SIZE;
    if (camera.pos.z > (float)WORLD_SIZE) camera.pos.z = (float)WORLD_SIZE;
    if (camera.pos.z < -(float)WORLD_SIZE) camera.pos.z = -(float)WORLD_SIZE;

    if (camera_moved_xz
            || camera_moved_y
            || camera_rotated) update_start_rays(camera_moved_xz, camera_moved_y, camera_rotated);
}

extern inline float sign(float num) {
    return (num >= 0.0f) ? 1.0f : -1.0f;
}

void next_edge(Ray* ray, int size, unsigned char octant) {

    float x_diff;
    float y_diff;
    float z_diff;

    if (ray->mx != 0.0f) {
        int next_edge_x = ((((octant & 1) == 0) ? 1 : -1) + (int)sign(ray->mx));
        if (next_edge_x > 1) next_edge_x = 1;
        else if (next_edge_x < -1) next_edge_x = -1;
        x_diff = fabs((next_edge_x*(float)size - ray->pos.x));
    } else {
        x_diff = INFINITY;
    }

    if (ray->my != 0.0f) {
        int next_edge_y = ((((octant & 2) == 0) ? 1 : -1) + (int)sign(ray->my));
        if (next_edge_y > 1) next_edge_y = 1;
        else if (next_edge_y < -1) next_edge_y = -1;
        y_diff = fabs((next_edge_y*(float)size - ray->pos.y));
    } else {
        y_diff = INFINITY;
    }

    if (ray->mz != 0.0f) {
        int next_edge_z = ((((octant & 4) == 0) ? 1 : -1) + (int)sign(ray->mz));
        if (next_edge_z > 1) next_edge_z = 1;
        else if (next_edge_z < -1) next_edge_z = -1;
        z_diff = fabs((next_edge_z*(float)size - ray->pos.z));
    } else {
        z_diff = INFINITY;
    }

    float x_t_value = fabs(x_diff * ray->inv_mx);
    float y_t_value = fabs(y_diff * ray->inv_my);
    float z_t_value = fabs(z_diff * ray->inv_mz);

    float minimum_t_value = (x_t_value < y_t_value) ? x_t_value : y_t_value;
    minimum_t_value = (minimum_t_value < z_t_value) ? minimum_t_value : z_t_value;

    if (minimum_t_value == x_t_value) {
        ray->pos.x += x_diff * sign(ray->mx);
        ray->pos.y += ray->my * minimum_t_value;
        ray->pos.z += ray->mz * minimum_t_value;
    } else if (minimum_t_value == y_t_value) {
        ray->pos.x += ray->mx * minimum_t_value;
        ray->pos.y += y_diff * sign(ray->my);
        ray->pos.z += ray->mz * minimum_t_value;
    } else if (minimum_t_value == z_t_value) {
        ray->pos.x += ray->mx * minimum_t_value;
        ray->pos.y += ray->my * minimum_t_value;
        ray->pos.z += z_diff * sign(ray->mz);
    }
}

Point3 translate_ray(Ray* ray, int size, unsigned char octant) {
    Point3 difference;

    difference.x = ((octant & 1) == 0) ? size : -size;
    difference.y = ((octant & 2) == 0) ? size : -size;
    difference.z = ((octant & 4) == 0) ? size : -size;

    ray->pos.x -= difference.x;
    ray->pos.y -= difference.y;
    ray->pos.z -= difference.z;

    return difference;
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
        return (ray.mx >= 0.0) ? (Point3){-1, 0, 0} : (Point3){1, 0, 0};
    } else if (is_integer(ray.pos.y)) {
        return (ray.my >= 0.0) ? (Point3){0, -1, 0} : (Point3){0, 1, 0};
    } else if (is_integer(ray.pos.z)) {
        return (ray.mz >= 0.0) ? (Point3){0, 0, -1} : (Point3){0, 0, 1};
    } else {
        return (Point3){0, 0, 0};
    }
}

extern inline float colour_from_ray(Ray ray) {
    Point3 normal = get_normal(ray);
    float dot_product = (light_height - ray.pos.x + world_space_translation.x) * normal.x + (0 - ray.pos.y + world_space_translation.y) * normal.y + (light_height - ray.pos.z + world_space_translation.z) * normal.z;
    dot_product *= inv_modulus((light_height - ray.pos.x + world_space_translation.x), (0 - ray.pos.y + world_space_translation.y), (light_height - ray.pos.z + world_space_translation.z));
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

extern inline unsigned char find_octant(Ray* ray, int size) {

    float translated_ray_pos_x = ray->pos.x - world_space_translation.x + WORLD_SIZE;
    float translated_ray_pos_y = ray->pos.y - world_space_translation.y + WORLD_SIZE;
    float translated_ray_pos_z = ray->pos.z - world_space_translation.z + WORLD_SIZE;

    int x_pos = translated_ray_pos_x;
    int y_pos = translated_ray_pos_y;
    int z_pos = translated_ray_pos_z;

    if (x_pos == translated_ray_pos_x && ray->mx < 0) x_pos--;
    if (y_pos == translated_ray_pos_y && ray->my < 0) y_pos--;
    if (z_pos == translated_ray_pos_z && ray->mz < 0) z_pos--;

    unsigned char octant = 0;
    if (!(x_pos & size)) octant += 1;
    if (!(y_pos & size)) octant += 2;
    if (!(z_pos & size)) octant += 4;

    return octant;
}

extern inline int out_of_bounds(Ray* ray, int size) {
    return (ray->pos.x > size) || (ray->pos.x < -size)
        || (ray->pos.x == size && ray->mx > 0) || (ray->pos.x == -size && ray->mx < 0)
        || (ray->pos.y > size) || (ray->pos.y < -size)
        || (ray->pos.y == size && ray->my > 0) || (ray->pos.y == -size && ray->my < 0)
        || (ray->pos.z > size) || (ray->pos.z < -size)
        || (ray->pos.z == size && ray->mz > 0) || (ray->pos.z == -size && ray->mz < 0);
}

void next_voxel_colour(Ray* ray, unsigned int octant_index, int size) {
    while (1) {
        if (out_of_bounds(ray, WORLD_SIZE)) {
            ray->collisions++;
            if (ray->collisions == 1)
                ray->colour = to_Colour(SKY);
            return;
        }
        if (out_of_bounds(ray, size)) return;
        unsigned char octant = find_octant(ray, size);

        if (octant_array[octant_index].occupancy & (1 << octant)) {
            if (size == 1) {
                ray->collisions++;
                if (ray->collisions == 1) {
                    ray->colour.r += voxel_array[octant_array[octant_index].voxel_array_index + octant].colour.r;
                    ray->colour.g += voxel_array[octant_array[octant_index].voxel_array_index + octant].colour.g;
                    ray->colour.b += voxel_array[octant_array[octant_index].voxel_array_index + octant].colour.b;
                    ray->colour = scale_Colour(ray->colour, colour_from_ray(*ray));
                    ray->mx = light_height - ray->pos.x + world_space_translation.x;
                    ray->my = 0 - ray->pos.y + world_space_translation.y;
                    ray->mz = light_height - ray->pos.z + world_space_translation.z;
                    ray->inv_mx = 1.0f / ray->mx;
                    ray->inv_my = 1.0f / ray->my;
                    ray->inv_mz = 1.0f / ray->mz;
                    next_voxel_colour(ray, octant_index, size);
                    return;
                } else if (ray->collisions == 2) {
                    ray->colour = scale_Colour(ray->colour, 0.5);
                    return;
                }
            }
            Point3 difference = translate_ray(ray, size>>1, octant);
            world_space_translation.x -= difference.x;
            world_space_translation.y -= difference.y;
            world_space_translation.z -= difference.z;
            next_voxel_colour(ray, octant_array[octant_index].octant_array_index + octant, size>>1);
            ray->pos.x += difference.x;
            ray->pos.y += difference.y;
            ray->pos.z += difference.z;
            world_space_translation.x += difference.x;
            world_space_translation.y += difference.y;
            world_space_translation.z += difference.z;
            if (ray->collisions < 2) continue;
            return;
        }

        next_edge(ray, size, octant);
    }
}

int render_pixel(int screen_x, int screen_y, Camera c) {

    Ray ray = start_rays[screen_x + screen_y*GAME_RES_WIDTH];
    world_space_translation = (Point3){0,0,0};
    next_voxel_colour(&ray, 0, WORLD_SIZE);
    Colour colour = ray.colour;

    return ((colour.r & 0xff) << 16) | ((colour.g & 0xff) << 8) | (colour.b & 0xff);
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

void fill_voxel(Octree* octree, int size, int x, int y, int z, Colour colour) {
    
    char octant_index = 0;
    unsigned char octant = 1;

    if (x < size) {
        octant_index += 1;
        octant <<= 1;
    }
    if (y < size) {
        octant_index += 2;
        octant <<= 2;
    }
    if (z < size) {
        octant_index += 4;
        octant <<= 4;
    }

    if (size == 1) {
        voxel_array[octree->voxel_array_index + octant_index].occupancy = 1;
        voxel_array[octree->voxel_array_index + octant_index].colour = colour;
        octree->occupancy |= octant;
        return;
    }

    if (x >= size) x -= size;
    if (y >= size) y -= size;
    if (z >= size) z -= size;

    fill_voxel(&(octant_array[octree->octant_array_index + octant_index]), size >> 1, x, y, z, colour);
    octree->occupancy |= octant;
}

void fill_to_height(Octree* octree, int x, int z, int height) {
    for (int y = 0; y <= height; y++) {
        fill_voxel(octree, WORLD_SIZE, x, y, z, to_Colour(0xffffff));
    }
}

void create_empty_octree(int size, unsigned int curr_octant_array_index, unsigned int* next_octant_array_index, unsigned int* next_voxel_array_index) {
    if (size == WORLD_SIZE) {
        octant_array = malloc((8*size*size*size - 1)/7 * sizeof(Octree));
        voxel_array = malloc(8*size*size*size*sizeof(Voxel));
    }

    if (size == 1) {
        octant_array[curr_octant_array_index].voxel_array_index = *next_voxel_array_index;
        curr_octant_array_index = *next_octant_array_index;
        *next_voxel_array_index += 8;
    } else {
        octant_array[curr_octant_array_index].octant_array_index = *next_octant_array_index;
        curr_octant_array_index = *next_octant_array_index;
        *next_octant_array_index += 8;
    }

    for (int i = 0; i < 8; i++) {
        if (size == 1) {
            voxel_array[curr_octant_array_index + i] = (Voxel){0, 0};
        } else {
            create_empty_octree(size >> 1, curr_octant_array_index + i, next_octant_array_index, next_voxel_array_index);
        }
    }
    return;
}

void create_world() {
    unsigned int octant_array_index = 1;
    unsigned int voxel_array_index = 0;
    create_empty_octree(WORLD_SIZE, 0, &octant_array_index, &voxel_array_index);

    for (int x = 0; x < WORLD_SIZE*2; x++) {
        for (int z = 0; z < WORLD_SIZE*2; z++) {
            fill_to_height(&octant_array[0], x, z, (int)(((float)WORLD_SIZE/8)*sin(x*8/(float)WORLD_SIZE) + ((float)WORLD_SIZE/8)*sin(z*8/(float)WORLD_SIZE) + (float)WORLD_SIZE/4));
        }
    }
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

    camera = (Camera){(Point3){0.0f, 0.0f, 0.0f}, -0.767945, 0, 0, 0, 0, 0, 2.0944, 0}; // -1.5708
    camera.cos_pitch = cos(camera.pitch);
    camera.sin_pitch = sin(camera.pitch);
    camera.cos_yaw = cos(camera.yaw);
    camera.sin_yaw = sin(camera.yaw);
    camera.focal_point = GAME_RES_WIDTH/2.0/tan(camera.fov/2.0)/SCREEN_SCALING_FACTOR;

    update_start_rays(1, 1, 1);
    for (int i = 0; i < GAME_RES_WIDTH*GAME_RES_HEIGHT-1; i++) {
        start_rays[i].collisions = 0;
    }

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

    light_height = WORLD_SIZE;

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
