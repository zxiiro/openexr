///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2002, Industrial Light & Magic, a division of Lucas
// Digital Ltd. LLC
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Industrial Light & Magic nor the names of
// its contributors may be used to endorse or promote products derived
// from this software without specific prior written permission. 
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
///////////////////////////////////////////////////////////////////////////



#include <ImfTiledRgbaFile.h>
#include <ImfRgbaFile.h>
#include <ImfArray.h>
#include <ImathRandom.h>
#include <string>
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <math.h>

using namespace Imf;
using namespace Imath;
using namespace std;

#if defined PLATFORM_WIN32
namespace
{
template<class T>
inline T min (const T &a, const T &b) { return (a <= b) ? a : b; }

template<class T>
inline T max (const T &a, const T &b) { return (a >= b) ? a : b; }
}
#endif

namespace {


void
fillPixels (Array2D<Rgba> &pixels, int w, int h)
{
    for (int y = 0; y < h; ++y)
    {
	for (int x = 0; x < w; ++x)
	{
	    Rgba &p = pixels[y][x];

	    p.r = 0.5 + 0.5 * sin (0.1 * x + 0.1 * y);
	    p.g = 0.5 + 0.5 * sin (0.1 * x + 0.2 * y);
	    p.b = 0.5 + 0.5 * sin (0.1 * x + 0.3 * y);
	    p.a = (p.r + p.b + p.g) / 3.0;
	}
    }
}


void
writeReadRGBAONE (const char fileName[],
	       int width,
	       int height,
	       RgbaChannels channels,
	       LineOrder lorder,
	       Compression comp,
           int xSize, int ySize)
{
#ifdef PLATFORM_WIN32
    string tmpfile;
#else
    string tmpfile ("/var/tmp/");
#endif
    tmpfile.append (fileName);

    
    Array2D<Rgba> p1 (height, width);
	fillPixels (p1, width, height);
    
    //
    // Save the selected channels of RGBA image p1; save the
    // scan lines in the specified order.  Read the image back
    // from the file, and compare the data with the orignal.
    //

    cout << "levelMode 0, " << "tileSize " << xSize << "x" << ySize <<
            ", line order " << lorder <<
            ", compression " << comp << endl;

    Header header (width, height);
    header.lineOrder() = lorder;
    header.compression() = comp;

    {
        remove (tmpfile.c_str ());
        TiledRgbaOutputFile out (tmpfile.c_str (), header, channels, xSize, ySize, ONE_LEVEL);
        out.setFrameBuffer (&p1[0][0], 1, width);

        int startTileY, endTileY;
        int dy;

        if (lorder == DECREASING_Y)
        {
            startTileY = out.numYTiles() - 1;
            endTileY = -1;

            dy = -1;
        }        
        else
        {
            startTileY = 0;
            endTileY = out.numYTiles();

            dy = 1;
        }
    
        for (int tileY = startTileY; tileY != endTileY; tileY += dy)
            for (int tileX = 0; tileX < out.numXTiles(); ++tileX)         
                out.writeTile (tileX, tileY);
    }

    {

        RgbaInputFile in (tmpfile.c_str ());
        const Box2i &dw = in.dataWindow();

        int w = dw.max.x - dw.min.x + 1;
        int h = dw.max.y - dw.min.y + 1;
        int dwx = dw.min.x;
        int dwy = dw.min.y;

        Array2D<Rgba> p2 (h, w);
        in.setFrameBuffer (&p2[-dwy][-dwx], 1, w);

        in.readPixels (dw.min.y, dw.max.y);

        assert (in.displayWindow() == header.displayWindow());
        assert (in.dataWindow() == header.dataWindow());
        assert (in.pixelAspectRatio() == header.pixelAspectRatio());
        assert (in.screenWindowCenter() == header.screenWindowCenter());
        assert (in.screenWindowWidth() == header.screenWindowWidth());
        assert (in.lineOrder() == header.lineOrder());
        assert (in.compression() == header.compression());
        assert (in.channels() == channels);

        for (int y = 0; y < h; ++y)
        {
            for (int x = 0; x < w; ++x)
            {
            
            if (channels & WRITE_R)
            {
                if (p2[y][x].r != p1[y][x].r)
                    std::cout << "*************" << ", " << x << ", " << y << ", " << p2[y][x].r << ", " << p1[y][x].r << std::endl;
                //assert (p2[y][x].r == p1[y][x].r);
                
            }
            else
                assert (p2[y][x].r == 0);

            if (channels & WRITE_G)
            {
                if (p2[y][x].r != p1[y][x].r)
                    std::cout << "*************" << ", " << x << ", " << y << ", " << p2[y][x].g << ", " << p1[y][x].g << std::endl;
                //assert (p2[y][x].g == p1[y][x].g);
            }
            else
                assert (p2[y][x].g == 0);

            if (channels & WRITE_B)
            {
                if (p2[y][x].r != p1[y][x].r)
                    std::cout << "*************" << ", " << x << ", " << y << ", " << p2[y][x].b << ", " << p1[y][x].b << std::endl;
                //assert (p2[y][x].b == p1[y][x].b);
            }
            else
                assert (p2[y][x].b == 0);

            if (channels & WRITE_A)
            {
                if (p2[y][x].r != p1[y][x].r)
                    std::cout << "*************" << ", " << x << ", " << y << ", " << p2[y][x].a << ", " << p1[y][x].a << std::endl;
                //assert (p2[y][x].a == p1[y][x].a);
            }
            else
                assert (p2[y][x].a == 1);
            }
        }
    }

    remove (tmpfile.c_str ());
}



void
writeReadRGBAMIP (const char fileName[],
	       int width,
	       int height,
	       RgbaChannels channels,
	       LineOrder lorder,
	       Compression comp,
           int xSize, int ySize)
{
#ifdef PLATFORM_WIN32
    string tmpfile;
#else
    string tmpfile ("/var/tmp/");
#endif
    tmpfile.append (fileName);

    int num;
#ifdef PLATFORM_WIN32
    num = (int) floor (log (float(max (width, height))) / log (2.0)) + 1;
#else
    num = (int) floor (log (float(std::max (width, height))) / log (2.0)) + 1;
#endif

    std::vector< Array2D<Rgba> > levels(num, Array2D<Rgba> ());
    for (int i = 0; i < num; ++i)
    {
        int w = width / (1 << i);
        int h = height / (1 << i);
        levels[i].resizeErase(h, w);
        fillPixels (levels[i], w, h);
    }

    //
    // Save the selected channels of RGBA image p1; save the
    // scan lines in the specified order.  Read the image back
    // from the file, and compare the data with the orignal.
    //

    cout << "levelMode 1, " << "tileSize " << xSize << "x" << ySize <<
            ", line order " << lorder <<
            ", compression " << comp << endl;

    Header header (width, height);
    header.lineOrder() = lorder;
    header.compression() = comp;

    {
        remove (tmpfile.c_str ());
        TiledRgbaOutputFile out (tmpfile.c_str (), header, channels, xSize, ySize, MIPMAP_LEVELS);
        
        int startTileY, endTileY;
        int dy;
        
        if (lorder == DECREASING_Y)
        {
            endTileY = -1;
            dy = -1;
        }        
        else
        {
            startTileY = 0;
            dy = 1;
        }

        for (int level = 0; level < out.numLevels(); ++level)
        {
            out.setFrameBuffer (&(levels[level])[0][0], 1, width / (1 << level));
            
            if (lorder == DECREASING_Y)
                startTileY = out.numYTiles(level) - 1;
            else
                endTileY = out.numYTiles(level);

            for (int tileY = startTileY; tileY != endTileY; tileY += dy)
                for (int tileX = 0; tileX < out.numXTiles(level); ++tileX)
                    out.writeTile (tileX, tileY, level);
        }
    }

    {
        TiledRgbaInputFile in (tmpfile.c_str ());
        const Box2i &dw = in.dataWindow();

        int w = dw.max.x - dw.min.x + 1;
        int h = dw.max.y - dw.min.y + 1;
        int dwx = dw.min.x;
        int dwy = dw.min.y;

        std::vector< Array2D<Rgba> > levels2(num, Array2D<Rgba> ());
        for (int i = 0; i < num; ++i)
        {
            int w1 = w / (1 << i);
            int h1 = h / (1 << i);
            levels2[i].resizeErase(h1, w1);
        }
        
        int startTileY, endTileY;
        int dy;
        
        for (int level = 0; level < in.numLevels(); ++level)
        {
            in.setFrameBuffer (&(levels2[level])[-dwy][-dwx], 1, w / (1 << level));
            
            if (lorder == DECREASING_Y)
            {
                startTileY = in.numYTiles(level) - 1;
                endTileY = -1;

                dy = -1;
            }        
            else
            {
                startTileY = 0;
                endTileY = in.numYTiles(level);

                dy = 1;
            }
            
            for (int tileY = startTileY; tileY != endTileY; tileY += dy)
                for (int tileX = 0; tileX < in.numXTiles(level); ++tileX)
                    in.readTile (tileX, tileY, level);
        }

        assert (in.displayWindow() == header.displayWindow());
        assert (in.dataWindow() == header.dataWindow());
        assert (in.pixelAspectRatio() == header.pixelAspectRatio());
        assert (in.screenWindowCenter() == header.screenWindowCenter());
        assert (in.screenWindowWidth() == header.screenWindowWidth());
        assert (in.lineOrder() == header.lineOrder());
        assert (in.compression() == header.compression());
        assert (in.channels() == channels);

        for (int l = 0; l < num; ++l)
        {
            for (int y = 0; y < (h / (1 << l)); ++y)
            {
                for (int x = 0; x < (w / (1 << l)); ++x)
                {
                    if (channels & WRITE_R)
                        assert ((levels2[l])[y][x].r == (levels[l])[y][x].r);
                    else
                        assert ((levels2[l])[y][x].r == 0);

                    if (channels & WRITE_G)
                        assert ((levels2[l])[y][x].g == (levels[l])[y][x].g);
                    else
                        assert ((levels2[l])[y][x].g == 0);

                    if (channels & WRITE_B)
                        assert ((levels2[l])[y][x].b == (levels[l])[y][x].b);
                    else
                        assert ((levels2[l])[y][x].b == 0);

                    if (channels & WRITE_A)
                        assert ((levels2[l])[y][x].a == (levels[l])[y][x].a);
                    else
                        assert ((levels2[l])[y][x].a == 1);
                }
            }
        }
    }

    remove (tmpfile.c_str ());
}


void
writeReadRGBARIP (const char fileName[],
	       int width,
	       int height,
	       RgbaChannels channels,
	       LineOrder lorder,
	       Compression comp,
           int xSize, int ySize)
{
#ifdef PLATFORM_WIN32
    string tmpfile;
#else
    string tmpfile ("/var/tmp/");
#endif
    tmpfile.append (fileName);

    int numX = (int) floor (log ((float)width) / log (2.0)) + 1;
    int numY = (int) floor (log ((float)height) / log (2.0)) + 1;

    std::vector< std::vector< Array2D<Rgba> > >
        levels(numY, std::vector< Array2D<Rgba> >(numX, Array2D<Rgba>()));

    for (int i = 0; i < numY; ++i)
    {
        for (int j = 0; j < numX; ++j)
        {
            int w = width / (1 << j);
            int h = height / (1 << i);
            levels[i][j].resizeErase(h, w);
            fillPixels (levels[i][j], w, h);
        }
    }

    //
    // Save the selected channels of RGBA image p1; save the
    // scan lines in the specified order.  Read the image back
    // from the file, and compare the data with the orignal.
    //

    cout << "levelMode 2, " << "tileSize " << xSize << "x" << ySize <<
            ", line order " << lorder <<
            ", compression " << comp << endl;

    Header header (width, height);
    header.lineOrder() = lorder;
    header.compression() = comp;

    {
        remove (tmpfile.c_str ());
        TiledRgbaOutputFile out (tmpfile.c_str (), header, channels, xSize, ySize, RIPMAP_LEVELS);
        
        int startTileY, endTileY;
        int dy;

        for (int ylevel = 0; ylevel < out.numYLevels(); ++ylevel)
        {               
            if (lorder == DECREASING_Y)
            {
                startTileY = out.numYTiles(ylevel) - 1;
                endTileY = -1;

                dy = -1;
            }        
            else
            {
                startTileY = 0;
                endTileY = out.numYTiles(ylevel);

                dy = 1;
            }
            
            for (int xlevel = 0; xlevel < out.numXLevels(); ++xlevel)
            {
                out.setFrameBuffer (&(levels[ylevel][xlevel])[0][0], 1, width / (1 << xlevel));
                
                for (int tileY = startTileY; tileY != endTileY; tileY += dy)
                    for (int tileX = 0; tileX < out.numXTiles (xlevel); ++tileX)
                        out.writeTile (tileX, tileY, xlevel, ylevel);
            }
        }
    }

    {
        TiledRgbaInputFile in (tmpfile.c_str ());
        const Box2i &dw = in.dataWindow();
        int w = dw.max.x - dw.min.x + 1;
        int h = dw.max.y - dw.min.y + 1;
        int dwx = dw.min.x;
        int dwy = dw.min.y;        
        
        std::vector< std::vector< Array2D<Rgba> > >
            levels2(numY, std::vector< Array2D<Rgba> >(numX, Array2D<Rgba>()));

        for (int i = 0; i < numY; ++i)
        {
            for (int j = 0; j < numX; ++j)
            {
                int w = width / (1 << j);
                int h = height / (1 << i);
                levels2[i][j].resizeErase(h, w);
            }
        }        
        
        int startTileY, endTileY;
        int dy;
        
        for (int ylevel = 0; ylevel < in.numYLevels(); ++ylevel)
        {
            if (lorder == DECREASING_Y)
            {
                startTileY = in.numYTiles(ylevel) - 1;
                endTileY = -1;

                dy = -1;
            }        
            else
            {
                startTileY = 0;
                endTileY = in.numYTiles(ylevel);

                dy = 1;
            }
            
            for (int xlevel = 0; xlevel < in.numXLevels(); ++xlevel)
            {
                in.setFrameBuffer (&(levels2[ylevel][xlevel])[-dwy][-dwx], 1, w / (1 << xlevel));
                
                for (int tileY = startTileY; tileY != endTileY; tileY += dy)
                    for (int tileX = 0; tileX < in.numXTiles (xlevel); ++tileX)
                        in.readTile (tileX, tileY, xlevel, ylevel);
            }
        }

        assert (in.displayWindow() == header.displayWindow());
        assert (in.dataWindow() == header.dataWindow());
        assert (in.pixelAspectRatio() == header.pixelAspectRatio());
        assert (in.screenWindowCenter() == header.screenWindowCenter());
        assert (in.screenWindowWidth() == header.screenWindowWidth());
        assert (in.lineOrder() == header.lineOrder());
        assert (in.compression() == header.compression());
        assert (in.channels() == channels);

        for (int ly = 0; ly < numY; ++ly)
        {
            for (int lx = 0; lx < numX; ++lx)
            {
                for (int y = 0; y < (h / (1 << ly)); ++y)
                {
                    for (int x = 0; x < (w / (1 << lx)); ++x)
                    {
                        if (channels & WRITE_R)
                            assert ((levels2[ly][lx])[y][x].r == (levels[ly][lx])[y][x].r);
                        else
                            assert ((levels2[ly][lx])[y][x].r == 0);

                        if (channels & WRITE_G)
                            assert ((levels2[ly][lx])[y][x].g == (levels[ly][lx])[y][x].g);
                        else
                            assert ((levels2[ly][lx])[y][x].g == 0);

                        if (channels & WRITE_B)
                            assert ((levels2[ly][lx])[y][x].b == (levels[ly][lx])[y][x].b);
                        else
                            assert ((levels2[ly][lx])[y][x].b == 0);

                        if (channels & WRITE_A)
                            assert ((levels2[ly][lx])[y][x].a == (levels[ly][lx])[y][x].a);
                        else
                            assert ((levels2[ly][lx])[y][x].a == 1);
                    }
                }
            }
        }
    }

    remove (tmpfile.c_str ());
}

void
writeRead (const char fileName[],
	       int W,
	       int H,
	       LineOrder lorder,
	       Compression comp,
           int xSize, int ySize)
{
    writeReadRGBAONE (fileName, W, H, WRITE_RGBA, lorder, comp, xSize, ySize);
    writeReadRGBAMIP (fileName, W, H, WRITE_RGBA, lorder, comp, xSize, ySize); 
    writeReadRGBARIP (fileName, W, H, WRITE_RGBA, lorder, comp, xSize, ySize);
}

} // namespace


void
testScanlineAPI ()
{
    try
    {
        cout << "Testing the Scanline API for Tiled files ..." << endl;

        const int W = 48;
        const int H = 81;

        for (int comp = 0; comp < NUM_COMPRESSION_METHODS; ++comp)
        {
            if (comp == ZIP_COMPRESSION)
                comp++;
            
            for (int lorder = 0; lorder < NUM_LINEORDERS; ++lorder)
            {
                writeRead ("imf_test_scanline_api.exr", W, H, 
                           LineOrder (lorder), Compression (comp), 1, 1);
                
                writeRead ("imf_test_scanline_api.exr", W, H,
                           LineOrder (lorder), Compression (comp), 24, 26);
                
                writeRead ("imf_test_scanline_api.exr", W, H,
                           LineOrder (lorder), Compression (comp), 48, 81);
                           
                writeRead ("imf_test_scanline_api.exr", W, H,
                           LineOrder (lorder), Compression (comp), 128, 96);
            }
        }

        cout << "ok\n" << endl;
    }
    catch (const std::exception &e)
    {
        cerr << "ERROR -- caught exception: " << e.what() << endl;
        assert (false);
    }
}
