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
#include <ImfArray.h>
#include <ImathRandom.h>
#include <string>
#include <stdio.h>
#include <assert.h>
#include <vector>
#include <math.h>
#include <sys/time.h>
#include <sys/resource.h>

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


double get_cpu_time()
{
    struct rusage t;

    getrusage(RUSAGE_SELF, &t);

    return (double)t.ru_utime.tv_sec + (double)t.ru_utime.tv_usec/1000000;
}


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
           int xSize, int ySize,
           bool triggerBuffering, bool triggerSeeks)
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

        int i;
        
        Rand32 rand1 = Rand32();
        std::vector<int> tileYs = std::vector<int>(out.numYTiles());
        std::vector<int> tileXs = std::vector<int>(out.numXTiles());
        for (i = 0; i < out.numYTiles(); i++)
        {
            if (lorder == DECREASING_Y)
                tileYs[out.numYTiles()-1-i] = i;    
            else
                tileYs[i] = i;
        }

        for (i = 0; i < out.numXTiles(); i++)
        {
            tileXs[i] = i;
        }
        
        if (triggerBuffering)
        {
            // shuffle the tile orders
            for (i = 0; i < out.numYTiles(); i++)
                std::swap(tileYs[i], tileYs[int(rand1.nextf(i,out.numYTiles()-1) + 0.5)]);

            for (i = 0; i < out.numXTiles(); i++)
                std::swap(tileXs[i], tileXs[int(rand1.nextf(i,out.numXTiles()-1) + 0.5)]);
        }

        for (int tileY = 0; tileY < out.numYTiles(); tileY++)
            for (int tileX = 0; tileX < out.numXTiles(); tileX++)
                out.writeTile (tileXs[tileX], tileYs[tileY]);
    }

    {
        TiledRgbaInputFile in (tmpfile.c_str ());
        const Box2i &dw = in.dataWindow();

        int w = dw.max.x - dw.min.x + 1;
        int h = dw.max.y - dw.min.y + 1;
        int dwx = dw.min.x;
        int dwy = dw.min.y;

        Array2D<Rgba> p2 (h, w);
        in.setFrameBuffer (&p2[-dwy][-dwx], 1, w);
        
        int startTileY, endTileY;
        int dy;

        if ((lorder == DECREASING_Y && !triggerSeeks) ||
            (lorder == INCREASING_Y && triggerSeeks) ||
            (lorder == RANDOM_Y && triggerSeeks))
        {
            startTileY = in.numYTiles() - 1;
            endTileY = -1;

            dy = -1;
        }        
        else
        {
            startTileY = 0;
            endTileY = in.numYTiles();

            dy = 1;
        }
    
        for (int tileY = startTileY; tileY != endTileY; tileY += dy)
            for (int tileX = 0; tileX < in.numXTiles(); ++tileX)
                in.readTile (tileX, tileY);

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
                assert (p2[y][x].r == p1[y][x].r);
            else
                assert (p2[y][x].r == 0);

            if (channels & WRITE_G)
                assert (p2[y][x].g == p1[y][x].g);
            else
                assert (p2[y][x].g == 0);

            if (channels & WRITE_B)
                assert (p2[y][x].b == p1[y][x].b);
            else
                assert (p2[y][x].b == 0);

            if (channels & WRITE_A)
                assert (p2[y][x].a == p1[y][x].a);
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
           int xSize, int ySize,
           bool triggerBuffering, bool triggerSeeks)
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
        
        int i;
        
        Rand32 rand1 = Rand32();
        std::vector<int> shuffled_levels = std::vector<int>(out.numLevels());
        
        for (i = 0; i < out.numLevels(); i++)
            shuffled_levels[i] = i;

        if (triggerBuffering)
            // shuffle the level order
            for (i = 0; i < out.numLevels(); i++)
                std::swap(shuffled_levels[i], shuffled_levels[int(rand1.nextf(i,out.numLevels()-1) + 0.5)]);

        for (int level = 0; level < out.numLevels(); ++level)
        {
            const int slevel = shuffled_levels[level];
            out.setFrameBuffer (&(levels[slevel])[0][0], 1, width / (1 << slevel));

            std::vector<int> tileYs = std::vector<int>(out.numYTiles(slevel));
            std::vector<int> tileXs = std::vector<int>(out.numXTiles(slevel));
            for (i = 0; i < out.numYTiles(slevel); i++)
            {
                if (lorder == DECREASING_Y)
                    tileYs[out.numYTiles(slevel)-1-i] = i;    
                else
                    tileYs[i] = i;
            }

            for (i = 0; i < out.numXTiles(slevel); i++)
                tileXs[i] = i;
            
            if (triggerBuffering)
            {
                // shuffle the tile orders
                for (i = 0; i < out.numYTiles(slevel); i++)
                    std::swap(tileYs[i], tileYs[int(rand1.nextf(i,out.numYTiles(slevel)-1) + 0.5)]);

                for (i = 0; i < out.numXTiles(slevel); i++)
                    std::swap(tileXs[i], tileXs[int(rand1.nextf(i,out.numXTiles(slevel)-1) + 0.5)]);
            }

            for (int tileY = 0; tileY < out.numYTiles(slevel); ++tileY)
                for (int tileX = 0; tileX < out.numXTiles(slevel); ++tileX)
                    out.writeTile (tileXs[tileX], tileYs[tileY], slevel);
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
            
            if ((lorder == DECREASING_Y && !triggerSeeks) ||
                (lorder == INCREASING_Y && triggerSeeks) ||
                (lorder == RANDOM_Y && triggerSeeks))
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
                for (int tileX = 0; tileX < in.numXTiles (level); ++tileX)
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
           int xSize, int ySize,
           bool triggerBuffering, bool triggerSeeks)
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
        
        int i;
        
        Rand32 rand1 = Rand32();
        std::vector<int> shuffled_xLevels = std::vector<int>(out.numXLevels());
        std::vector<int> shuffled_yLevels = std::vector<int>(out.numYLevels());
        
        for (i = 0; i < out.numXLevels(); i++)
            shuffled_xLevels[i] = i;
        
        for (i = 0; i < out.numYLevels(); i++)
            shuffled_yLevels[i] = i;

        if (triggerBuffering)
        {
            // shuffle the level orders
            for (i = 0; i < out.numXLevels(); i++)
                std::swap(shuffled_xLevels[i], shuffled_xLevels[int(rand1.nextf(i,out.numXLevels()-1) + 0.5)]);
                
            for (i = 0; i < out.numYLevels(); i++)
                std::swap(shuffled_yLevels[i], shuffled_yLevels[int(rand1.nextf(i,out.numYLevels()-1) + 0.5)]);
        }

        for (int ylevel = 0; ylevel < out.numYLevels(); ++ylevel)
        {
            const int sylevel = shuffled_yLevels[ylevel];
            
            std::vector<int> tileYs = std::vector<int>(out.numYTiles(sylevel));
            for (i = 0; i < out.numYTiles(sylevel); i++)
            {
                if (lorder == DECREASING_Y)
                    tileYs[out.numYTiles(sylevel)-1-i] = i;    
                else
                    tileYs[i] = i;
            }
            
            if (triggerBuffering)
                // shuffle the tile orders
                for (i = 0; i < out.numYTiles(sylevel); i++)
                    std::swap(tileYs[i], tileYs[int(rand1.nextf(i,out.numYTiles(sylevel)-1) + 0.5)]);
            
            for (int xlevel = 0; xlevel < out.numXLevels(); ++xlevel)
            {
                const int sxlevel = shuffled_xLevels[xlevel];
                
                out.setFrameBuffer (&(levels[sylevel][sxlevel])[0][0], 1, width / (1 << sxlevel));
                
                std::vector<int> tileXs = std::vector<int>(out.numXTiles(sxlevel));
                for (i = 0; i < out.numXTiles(sxlevel); i++)
                    tileXs[i] = i;

                if (triggerBuffering)
                    // shuffle the tile orders
                    for (i = 0; i < out.numXTiles(sxlevel); i++)
                        std::swap(tileXs[i], tileXs[int(rand1.nextf(i,out.numXTiles(sxlevel)-1) + 0.5)]);
                
                for (int tileY = 0; tileY < out.numYTiles(sylevel); ++tileY)
                    for (int tileX = 0; tileX < out.numXTiles(sxlevel); ++tileX)
                        out.writeTile(tileXs[tileX], tileYs[tileY], sxlevel, sylevel);
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
        
            if ((lorder == DECREASING_Y && !triggerSeeks) ||
                (lorder == INCREASING_Y && triggerSeeks) ||
                (lorder == RANDOM_Y && triggerSeeks))
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
           int xSize, int ySize,
           bool triggerBuffering, bool triggerSeeks)
{
    writeReadRGBAONE ("imf_test_tiled_rgba.exr",
                      W, H, WRITE_RGBA, LineOrder (lorder), Compression (comp),
                      xSize, ySize, triggerBuffering, triggerSeeks);

    writeReadRGBAMIP ("imf_test_tiled_rgba.exr",
                      W, H, WRITE_RGBA, LineOrder (lorder), Compression (comp),
                      xSize, ySize, triggerBuffering, triggerSeeks);

    writeReadRGBARIP ("imf_test_tiled_rgba.exr",
                      W, H, WRITE_RGBA, LineOrder (lorder), Compression (comp),
                      xSize, ySize, triggerBuffering, triggerSeeks);
}

} // namespace


void
testTiledRgba ()
{
    try
    {
        cout << "Testing the Tiled/Multi-Resolution RGBA image interface" << endl;

        const int W = 75;
        const int H = 52;


        double t = get_cpu_time();
        for (int comp = 0; comp < NUM_COMPRESSION_METHODS; ++comp)
        {
            if (comp == ZIP_COMPRESSION)
                comp++;
            
            for (int lorder = 0; lorder < NUM_LINEORDERS; ++lorder)
            {                
                writeRead ("imf_test_tiled_rgba.exr",
                           W, H, LineOrder (lorder), Compression (comp),
                           1, 1, false, false);
                
                writeRead ("imf_test_tiled_rgba.exr",
                           W, H, LineOrder (lorder), Compression (comp),
                           35, 26, false, false);
                           
                writeRead ("imf_test_tiled_rgba.exr",
                           W, H, LineOrder (lorder), Compression (comp),
                           75, 52, false, false);
                           
                writeRead ("imf_test_tiled_rgba.exr",
                           W, H, LineOrder (lorder), Compression (comp),
                           264, 129, false, false);
            }
        }
        std::cout << "time = " << get_cpu_time() - t << std::endl;

        cout << "ok\n" << endl;
    }
    catch (const std::exception &e)
    {
        cerr << "ERROR -- caught exception: " << e.what() << endl;
        assert (false);
    }
}
