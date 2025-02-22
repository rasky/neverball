/*
 * Copyright (C) 2003 Robert Kooima
 *
 * NEVERBALL is  free software; you can redistribute  it and/or modify
 * it under the  terms of the GNU General  Public License as published
 * by the Free  Software Foundation; either version 2  of the License,
 * or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 */

#include <SDL.h>
#include <SDL_ttf.h>
#include <string.h>
#include <math.h>
#include <png.h>
#include <stdlib.h>

#include "glext.h"
#include "image.h"
#include "base_image.h"
#include "config.h"
#include "video.h"

#include "fs.h"
#include "fs_png.h"

/*---------------------------------------------------------------------------*/

void image_snap(const char *filename)
{
    fs_file     filep  = NULL;
    png_structp writep = NULL;
    png_infop   infop  = NULL;
    png_bytep  *bytep  = NULL;

    int w = video.device_w;
    int h = video.device_h;
    int i;

    unsigned char *p = NULL;

    /* Initialize all PNG export data structures. */

    if (!(filep = fs_open_write(filename)))
        return;
    if (!(writep = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0)))
        return;
    if (!(infop = png_create_info_struct(writep)))
        return;

    /* Enable the default PNG error handler. */

    if (setjmp(png_jmpbuf(writep)) == 0)
    {
        /* Initialize the PNG header. */

        png_set_write_fn(writep, filep, fs_png_write, fs_png_flush);
        png_set_IHDR(writep, infop, w, h, 8,
                     PNG_COLOR_TYPE_RGB,
                     PNG_INTERLACE_NONE,
                     PNG_COMPRESSION_TYPE_DEFAULT,
                     PNG_FILTER_TYPE_DEFAULT);

        /* Allocate the pixel buffer and copy pixels there. */

        if ((p = (unsigned char *) malloc(w * h * 4)))
        {
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, p);

            /* Allocate and initialize the row pointers. */

            if ((bytep = (png_bytep *) png_malloc(writep, h * sizeof (png_bytep))))
            {
                for (i = 0; i < h; ++i)
                    bytep[h - i - 1] = (png_bytep) (p + i * w * 4);

                /* Write the PNG image file. */

                png_write_info(writep, infop);
                png_set_filler(writep, 0, PNG_FILLER_AFTER);
                png_write_image(writep, bytep);
                png_write_end(writep, infop);

                free(bytep);
            }
            free(p);
        }
    }

    /* Release all resources. */

    png_destroy_write_struct(&writep, &infop);
    fs_close(filep);
}

/*---------------------------------------------------------------------------*/

/*
 * Create an OpenGL texture object using the given image buffer.
 */
GLuint make_texture(const void *p, int w, int h, int b, int fl)
{
    static const GLenum format[] =
        { 0, GL_LUMINANCE, GL_LUMINANCE_ALPHA, GL_RGB, GL_RGBA };

    GLuint o = 0;

    /* Scale the image as configured, or to fit the OpenGL limitations. */

#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
    int a = config_get_d(CONFIG_ANISO);
#endif
#ifdef GL_GENERATE_MIPMAP_SGIS
    int m = (fl & IF_MIPMAP) ? config_get_d(CONFIG_MIPMAP) : 0;
#endif
    int k = config_get_d(CONFIG_TEXTURES);
    int W = w;
    int H = h;

    GLint max = gli.max_texture_size;

    void *q = NULL;

    while (w / k > (int) max || h / k > (int) max)
        k *= 2;

    if (k > 1)
        q = image_scale(p, w, h, b, &W, &H, k);

    /* Generate and configure a new OpenGL texture. */

    glGenTextures(1, &o);
    glBindTexture(GL_TEXTURE_2D, o);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

#ifdef GL_GENERATE_MIPMAP_SGIS
    if (m)
    {
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP_SGIS, GL_TRUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        GL_LINEAR_MIPMAP_LINEAR);
    }
#endif
#ifdef GL_TEXTURE_MAX_ANISOTROPY_EXT
    if (a && gli.texture_filter_anisotropic) glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, a);
#endif

    /* Copy the image to an OpenGL texture. */

    glTexImage2D(GL_TEXTURE_2D, 0,
                 format[b], W, H, 0,
                 format[b], GL_UNSIGNED_BYTE, q ? q : p);

    if (q) free(q);


    return o;
}

/*
 * Load an image from the named file.  Return an OpenGL texture object.
 */
GLuint make_image_from_file(const char *filename, int fl)
{
    void  *p;
    int    w;
    int    h;
    int    b;
    GLuint o = 0;

    /* Load the image. */

    if ((p = image_load(filename, &w, &h, &b)))
    {
        o = make_texture(p, w, h, b, fl);
        free(p);
    }

    return o;
}

/*---------------------------------------------------------------------------*/

/*
 * Render the given  string using the given font.   Transfer the image
 * to a  surface of  power-of-2 size large  enough to fit  the string.
 * Return an OpenGL texture object.
 */
GLuint make_image_from_font(int *W, int *H,
                            int *w, int *h,
                            const char *text, TTF_Font *font, int fl)
{
    GLuint o = 0;

    /* Render the text. */

    if (font && text && strlen(text) > 0)
    {
        SDL_Color    col = { 0xFF, 0xFF, 0xFF, 0xFF };
        SDL_Surface *orig;

        if ((orig = TTF_RenderUTF8_Blended(font, text, col)))
        {
            void *p;
            int  w2;
            int  h2;
            int   b = orig->format->BitsPerPixel / 8;

            SDL_Surface *src;
            SDL_PixelFormat fmt;

            fmt = *orig->format;

            fmt.Rmask = RMASK;
            fmt.Gmask = GMASK;
            fmt.Bmask = BMASK;
            fmt.Amask = AMASK;

            if ((src = SDL_ConvertSurface(orig, &fmt, orig->flags)) == NULL)
            {
                /* Pretend everything's just fine. */

                src = orig;
            }
            else
                SDL_FreeSurface(orig);

            /* Pad the text to power-of-two. */

            p = image_next2(src->pixels, src->w, src->h, b, &w2, &h2);

            if (w) *w = src->w;
            if (h) *h = src->h;
            if (W) *W = w2;
            if (H) *H = h2;

            /* Saturate the color channels.  Modulate ONLY in alpha. */

            image_white(p, w2, h2, b);

            /* Create the OpenGL texture object. */

            o = make_texture(p, w2, h2, b, fl);

            free(p);
            SDL_FreeSurface(src);
        }
    }
    else
    {
        /* Empty string. */

        if (w) *w = 0;
        if (h) *h = 0;
        if (W) *W = 0;
        if (H) *H = 0;
    }

    return o;
}

/*
 * Measure text without rendering it.
 */
void size_image_from_font(int *W, int *H,
                          int *w, int *h,
                          const char *text, TTF_Font *font)
{
    int text_w, text_h, w2, h2;

    TTF_SizeUTF8(font, text, &text_w, &text_h);

    if (w) *w = text_w;
    if (h) *h = text_h;

    image_size(&w2, &h2, text_w, text_h);

    if (W) *W = w2;
    if (H) *H = h2;
}

/*---------------------------------------------------------------------------*/

/*
 * Load an image from the named file.  Return an SDL surface.
 */
SDL_Surface *load_surface(const char *filename)
{
    void  *p;
    int    w;
    int    h;
    int    b;

    SDL_Surface *srf = NULL;

    if ((p = image_load(filename, &w, &h, &b)))
    {
        void *q;

        if ((q = image_flip(p, w, h, b, 0, 1)))
        {
            srf = SDL_CreateRGBSurfaceFrom(q, w, h, b * 8, w * b,
                                           RMASK, GMASK, BMASK, AMASK);
        }
        free(p);
    }
    return srf;
}

/*---------------------------------------------------------------------------*/
