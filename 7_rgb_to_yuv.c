/*
 * gcc make_yuv.c -o make_yuv
 * ./make_yuv
 * 生成文件: yellow_red_64x64.yuv
 * @note 用于了解 yuv420p 文件格式，以及和 rgb 图像元素的对应关系
 * @note: ffplay -f rawvideo -pixel_format yuv420p -video_size 64x64 yellow_red_64x64.yuv 可查看 yuv 图片内容
 */

 #include <stdint.h>
 #include <stdio.h>
 #include <stdlib.h>
 
 #ifdef USE_LIBYUV
 #include <libyuv.h>
 #endif
 

 #define W 64
 #define H 64
 #define HALF_W (W/2)
 #define HALF_H (H/2)
 
 /* 标准 BT.601 转换系数 */
 static inline uint8_t RGB_TO_Y(uint8_t r, uint8_t g, uint8_t b)
 {
     return (uint8_t)( ( 66 * r + 129 * g +  25 * b + 128) >> 8) + 16;
 }
 static inline uint8_t RGB_TO_U(uint8_t r, uint8_t g, uint8_t b)
 {
     return (uint8_t)( (-38 * r -  74 * g + 112 * b + 128) >> 8) + 128;
 }
 static inline uint8_t RGB_TO_V(uint8_t r, uint8_t g, uint8_t b)
 {
     return (uint8_t)( (112 * r -  94 * g -  18 * b + 128) >> 8) + 128;
 }
 
 int main(void)
 {
     /* 申请 Y,U,V 三个平面 */
     uint8_t *y = malloc(W * H);
     uint8_t *u = malloc(HALF_W * HALF_H);
     uint8_t *v = malloc(HALF_W * HALF_H);
 
     /* 1. 填充 Y */
     for (int row = 0; row < H; ++row) {
         for (int col = 0; col < W; ++col) {
             if (col < W/2) {               /* 左侧黄色 */
                 y[row * W + col] = RGB_TO_Y(255, 255, 0);
             } else {                       /* 右侧红色 */
                 y[row * W + col] = RGB_TO_Y(255, 0,   0);
             }
         }
     }
 
     /* 2. 填充 U/V（2×2 平均后下采样） */
     for (int row = 0; row < HALF_H; ++row) {
         for (int col = 0; col < HALF_W; ++col) {
             int even_row = row * 2;
             int even_col = col * 2;
 
             /* 取 2×2 块左上角像素的颜色做近似 */
             uint8_t r, g, b;
             if (even_col < W/2) {          /* 黄色 */
                 r = 255; g = 255; b = 0;
             } else {                       /* 红色 */
                 r = 255; g = 0;   b = 0;
             }
 
             u[row * HALF_W + col] = RGB_TO_U(r, g, b);
             v[row * HALF_W + col] = RGB_TO_V(r, g, b);
         }
     }
 
     /* 3. 写入原始文件（保持不变） */
     FILE *fp = fopen("yellow_red_64x64.yuv", "wb");
     if (!fp) { perror("fopen"); return 1; }
 
     fwrite(y, 1, W * H, fp);
     fwrite(u, 1, HALF_W * HALF_H, fp);
     fwrite(v, 1, HALF_W * HALF_H, fp);
     fclose(fp);
 
     /* ===== 使用 libyuv 做水平镜像 ===== */
 #if defined(__has_include) && __has_include(<libyuv.h>)
     uint8_t *y_flip = malloc(W * H);
     uint8_t *u_flip = malloc(HALF_W * HALF_H);
     uint8_t *v_flip = malloc(HALF_W * HALF_H);
 
     I420Mirror(y, W,
                u, HALF_W,
                v, HALF_W,
                y_flip, W,
                u_flip, HALF_W,
                v_flip, HALF_W,
                W, H);
 
     FILE *fp_flip = fopen("red_yellow_64x64.yuv", "wb");
     if (!fp_flip) { perror("fopen"); return 1; }
 
     fwrite(y_flip, 1, W * H, fp_flip);
     fwrite(u_flip, 1, HALF_W * HALF_H, fp_flip);
     fwrite(v_flip, 1, HALF_W * HALF_H, fp_flip);
     fclose(fp_flip);
 
     free(y_flip); free(u_flip); free(v_flip);
 #endif
     /* ===== 新增结束 ===== */
 
     free(y); free(u); free(v);
     puts("yellow_red_64x64.yuv 已生成");
 #if defined(__has_include) && __has_include(<libyuv.h>)
     puts("red_yellow_64x64.yuv 已生成（水平镜像，libyuv）");
 #endif
     return 0;
 }