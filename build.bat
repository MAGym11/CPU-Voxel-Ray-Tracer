@echo off
gcc voxel_ray_tracer.c -O3 -march=native -lgdi32 -o voxel_ray_tracer.exe && voxel_ray_tracer
