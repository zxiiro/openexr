///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2003, Industrial Light & Magic, a division of Lucas
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



//-----------------------------------------------------------------------------
//
//	Miscellaneous tiled image file related stuff
//
//-----------------------------------------------------------------------------

#include <ImfTiledMisc.h>
#include <Iex.h>
#include <ImfMisc.h>
#include <ImfChannelList.h>

#if defined PLATFORM_WIN32
namespace
{
template<class T>
inline T min (const T &a, const T &b) { return (a <= b) ? a : b; }

template<class T>
inline T max (const T &a, const T &b) { return (a >= b) ? a : b; }
}
#endif

namespace Imf {

using Imath::Box2i;
using Imath::V2i;

int
levelSize (int min, int max, int l)
{
    if (l < 0)
	throw Iex::ArgExc ("Parameter not in valid range.");

#ifdef PLATFORM_WIN32
    return max((max - min + 1) / (1 << l), 1);
#else
    return std::max((max - min + 1) / (1 << l), 1);
#endif
}


Box2i
dataWindowForLevel (int minX, int maxX,
		    int minY, int maxY,
		    int lx, int ly)
{
    V2i levelMin = V2i(minX, minY);
    V2i levelMax = levelMin + V2i(levelSize(minX, maxX, lx) - 1,
				  levelSize(minY, maxY, ly) - 1);

    return Box2i(levelMin, levelMax);
}


Box2i
dataWindowForTile (int minX, int maxX,
		   int minY, int maxY,
		   int tileXSize, int tileYSize,
		   int dx, int dy,
		   int lx, int ly)
{
    V2i tileMin = V2i(minX + dx * tileXSize,
		      minY + dy * tileYSize);

    V2i tileMax = tileMin + V2i(tileXSize - 1, tileYSize - 1);

    V2i levelMax = dataWindowForLevel(minX, maxX, minY, maxY, lx, ly).max;

#ifdef PLATFORM_WIN32
    tileMax = V2i(min(tileMax[0], levelMax[0]), min(tileMax[1], levelMax[1]));
#else
    tileMax = V2i(std::min(tileMax[0], levelMax[0]),
		  std::min(tileMax[1], levelMax[1]));
#endif
    return Box2i(tileMin, tileMax);
}


size_t
calculateMaxBytesPerLineForTile(const Header &header, int tileXSize)
{
    const ChannelList &channels = header.channels();

    size_t maxBytesPerTileLine = 0;
    for (ChannelList::ConstIterator c = channels.begin();
	 c != channels.end(); ++c)
    {
	maxBytesPerTileLine += pixelTypeSize(c.channel().type) * tileXSize;
    }

    return maxBytesPerTileLine;
}

namespace {

int
calculateNumXLevels(const TileDescription& tileDesc,
		    int minX, int maxX, int minY, int maxY)
{
    int num = 0;
    switch (tileDesc.mode)
    {
      case ONE_LEVEL:

	num = 1;
	break;

      case MIPMAP_LEVELS:

	{
	  int w = maxX - minX + 1;
	  int h = maxY - minY + 1;
#ifdef PLATFORM_WIN32
	  num = (int) floor (log (max (w, h)) / log (2)) + 1;
#else
	  num = (int) floor (log (std::max (w, h)) / log (2)) + 1;
#endif
	}
        break;

      case RIPMAP_LEVELS:

	{
	  int w = maxX - minX + 1;
	  num = (int)floor (log (w) / log (2)) + 1;
	}
	break;

      default:

	throw Iex::ArgExc ("Unknown LevelMode format.");
    }

    return num;
}


int
calculateNumYLevels(const TileDescription& tileDesc,
		    int minX, int maxX, int minY, int maxY)
{
    int num = 0;
    switch (tileDesc.mode)
    {
      case ONE_LEVEL:

	num = 1;
	break;

      case MIPMAP_LEVELS:

	{
	  int w = maxX - minX + 1;
	  int h = maxY - minY + 1;
#ifdef PLATFORM_WIN32
	  num = (int) floor (log (max (w, h)) / log (2)) + 1;
#else
	  num = (int) floor (log (std::max (w, h)) / log (2)) + 1;
#endif
	}
        break;

      case RIPMAP_LEVELS:

	{
	  int h = maxY - minY + 1;
	  num = (int)floor (log (h) / log (2)) + 1;
	}
	break;

      default:

	throw Iex::ArgExc ("Unknown LevelMode format.");
    }

    return num;
}


void
calculateNumXTiles(int* numXTiles, int numXLevels,
		   int minX, int maxX, int xSize)
{
    for (int i = 0; i < numXLevels; i++)
    {
	numXTiles[i] = (levelSize(minX, maxX, i) + xSize - 1) / xSize;
    }
}


void
calculateNumYTiles(int* numYTiles, int numYLevels,
		   int minY, int maxY, int ySize)
{
    for (int i = 0; i < numYLevels; i++)
    {
	numYTiles[i] = (levelSize(minY, maxY, i) + ySize - 1) / ySize;
    }
}


} // namespace


void
precalculateTileInfo(const TileDescription& tileDesc,
		     int minX, int maxX, int minY, int maxY,
		     int*& numXTiles, int*& numYTiles,
		     int& numXLevels, int& numYLevels)
{
    numXLevels = calculateNumXLevels(tileDesc, minX, maxX, minY, maxY);
    numYLevels = calculateNumYLevels(tileDesc, minX, maxX, minY, maxY);
    
    numXTiles = new int[numXLevels];
    numYTiles = new int[numYLevels];
    calculateNumXTiles(numXTiles, numXLevels, minX, maxX, tileDesc.xSize);
    calculateNumYTiles(numYTiles, numYLevels, minY, maxY, tileDesc.ySize);
}


} // namespace Imf
