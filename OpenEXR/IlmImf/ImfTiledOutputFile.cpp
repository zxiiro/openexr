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



//-----------------------------------------------------------------------------
//
//	class TiledOutputFile
//
//-----------------------------------------------------------------------------

#include <ImfTiledOutputFile.h>
#include <ImfTiledInputFile.h>
#include <ImfInputFile.h>
#include <ImfTileDescriptionAttribute.h>
#include <ImfChannelList.h>
#include <ImfMisc.h>
#include <ImfIO.h>
#include <ImfCompressor.h>
#include <ImathBox.h>
#include <ImfArray.h>
#include <ImfXdr.h>
#include <Iex.h>
#include <string>
#include <vector>
#include <fstream>
#include <assert.h>
#include <map>


//FIXME, don't return in a catch

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
using std::string;
using std::vector;
using std::ofstream;
using std::map;


namespace {


struct OutSliceInfo
{
    PixelType		type;
    const char *	base;
    size_t		xStride;
    size_t		yStride;
    bool		zero;

    OutSliceInfo (PixelType type = HALF,
	          const char *base = 0,
	          size_t xStride = 0,
	          size_t yStride = 0,
	          bool zero = false);
};


OutSliceInfo::OutSliceInfo (PixelType t,
		            const char *b,
		            size_t xs, size_t ys,
		            bool z)
:
    type (t),
    base (b),
    xStride (xs),
    yStride (ys),
    zero (z)
{
    // empty
}


struct TileCoord
{
    int dx, dy, lx, ly;
    
    TileCoord(int xTile = 0, int yTile = 0, int xLevel = 0, int yLevel = 0) :
        dx(xTile), dy(yTile), lx(xLevel), ly(yLevel)
    {
        // empty
    }
    
    bool operator<(const TileCoord& a) const
    {
        return ((ly < a.ly) || (ly == a.ly && lx < a.lx) ||
                 ((ly == a.ly && lx == a.lx) &&
                  ((dy < a.dy) || (dy == a.dy && dx < a.dx))));
    }

    // FIXME, maybe go away
    bool operator==(const TileCoord& a) const
    {
        return (lx == a.lx && ly == a.ly && dx == a.dx && dy == a.dy);
    }
};


struct BufferedTile
{
    char * pixelData;
    int pixelDataSize;

    // FIXME, allocate memory here, add parameters for pixelDataSize
    BufferedTile() : pixelData(0), pixelDataSize(0)
    {
        // empty
    }

    // TODO, add destructor that deallocates the memory
};


} // namespace

struct TiledOutputFile::Data
{
public:
    string		fileName;		// the filename
    Header		header;			// the image header
    TileDescription	tileDesc;		// describes the tile layout
    FrameBuffer		frameBuffer;		// framebuffer to write into
    LineOrder		lineOrder;		// the file's lineorder
    int			minX;			// data window's min x coord
    int			maxX;			// data window's max x coord
    int			minY;			// data window's min y coord
    int			maxY;			// data window's max x coord

    //
    // cached tile information:
    //
    int			numXLevels;		// number of x levels
    int			numYLevels;		// number of y levels
    int*		numXTiles;		// number of x tiles at a level
    int*		numYTiles;		// number of y tiles at a level

    // FIXME, make a 3d or 4d class for these
    vector<vector<vector <long> > > tileOffsets; // stores offsets in file for
						 // each tile

    Compressor *	compressor;		// the compressor
    Compressor::Format	format;			// compressor's data format
    vector<OutSliceInfo> slices;		// info about channels in file
    ofstream		os;			// file stream to write to

    size_t		maxBytesPerTileLine;	// combined size of a tile line
						// over all channels

    size_t		tileBufferSize;		// size of the tile buffer
    Array<char>		tileBuffer;		// holds a single tile

    long		tileOffsetsPosition;	// position of the tile index
    long		currentPosition;	// current position in the file
    
    map <TileCoord, BufferedTile*> tileMap;
    TileCoord nextTileToWrite;
    
    TileCoord nextTileCoord(const TileCoord& a);

    Data (): numXTiles(0), numYTiles(0), compressor(0) {}

    ~Data ()
    {
        delete numXTiles;
        delete numYTiles;
        delete compressor;
        
        
        //
        // Delete all the tile buffers, if any still happen to exist
        //
        
        map<TileCoord, BufferedTile*>::iterator i;
        
        for (i = tileMap.begin(); i != tileMap.end(); )
        {
            delete [] i->second;
	    // FIXME, get rid of erases
            tileMap.erase(i++);
        }
    
    }
    

    //
    // Precomputation and caching of various tile parameters
    //
    
    void		precomputeNumXLevels ();
    void		precomputeNumYLevels ();
    void		precomputeNumXTiles ();
    void		precomputeNumYTiles ();
    
    void		calculateMaxBytesPerLineForTile();
};

TileCoord
TiledOutputFile::Data::nextTileCoord(const TileCoord& a)
{
    TileCoord b = a;
    
    if (lineOrder == INCREASING_Y)
    {
        b.dx++;
        if (b.dx >= numXTiles[b.lx])
        {
            b.dx = 0;
            b.dy++;

            // the next tile is in the next level
            if (b.dy >= numYTiles[b.ly])
            {
                b.dy = 0;

                switch (tileDesc.mode)
                {
                  case ONE_LEVEL:
                  case MIPMAP_LEVELS:

                    b.lx++;
                    b.ly++;
                    break;

                  case RIPMAP_LEVELS:

                    b.lx++;
                    if (b.lx >= numXLevels)
                    {
                        b.lx = 0;
                        b.ly++;

			// FIXME, add assert
                        //if (b.ly > numYLevels)
                            // should not happen
                    }
                    break;
                }
            }
        }
    }
    else if (lineOrder == DECREASING_Y)
    {
        b.dx++;

        if (b.dx >= numXTiles[b.lx])
        {
            b.dx = 0;
            b.dy--;

            // the next tile is in the next level
            if (b.dy < 0)
            {
                switch (tileDesc.mode)
                {
                  case ONE_LEVEL:
                  case MIPMAP_LEVELS:

                    b.lx++;
                    b.ly++;
                    break;

                  case RIPMAP_LEVELS:

                    b.lx++;
                    if (b.lx >= numXLevels)
                    {
                        b.lx = 0;
                        b.ly++;

			// FIXME, add assert
                        //if (b.ly > numYLevels)
                            // should not happen
                    }
                    break;
                }
                b.dy = numYTiles[b.ly] - 1;
            }
        }
    }
    
    return b;   
}

// FIXME, put all precomputation in one function
//
// precomputation and caching of various tile parameters
//
void
TiledOutputFile::Data::precomputeNumXLevels()
{
    try
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

	numXLevels = num;
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Cannot compute numXLevels on image "
		        "file \"" << fileName << "\". " << e);
	throw;
    }
}


void
TiledOutputFile::Data::precomputeNumYLevels()
{
    try
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

	numYLevels = num;
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error calling numXLevels() on image "
		        "file \"" << fileName << "\". " << e);
	throw;
    }
}


void
TiledOutputFile::Data::precomputeNumXTiles()
{
    delete numXTiles;
    numXTiles = new int[numXLevels];
	
    for (int i = 0; i < numXLevels; i++)
    {
    	// FIXME, take the max of the level's width, with 1
	// FIXME, change pows to shifts
	numXTiles[i] = ((maxX - minX + 1) / (int) pow(2, i) +
			tileDesc.xSize - 1) / tileDesc.xSize;
    }
}


void
TiledOutputFile::Data::precomputeNumYTiles()
{
    delete numYTiles;
    numYTiles = new int[numYLevels];

    for (int i = 0; i < numYLevels; i++)
    {
	// FIXME, take the max of the level's height, with 1
	// FIXME, change pows to shifts
	numYTiles[i] = ((maxY - minY + 1) / (int) pow(2, i) +
			tileDesc.ySize - 1) / tileDesc.ySize;
    }
}


void
TiledOutputFile::Data::calculateMaxBytesPerLineForTile()
{
    const ChannelList &channels = header.channels();

    maxBytesPerTileLine = 0;
    for (ChannelList::ConstIterator c = channels.begin();
	 c != channels.end(); ++c)
    {
	maxBytesPerTileLine += pixelTypeSize(c.channel().type) * tileDesc.xSize;
    }
}


namespace {


long
tileOffset(TiledOutputFile::Data const *ofd, int dx, int dy, int lx, int ly)
{
    //
    // Looks up the value of the tile with tile coordinate (dx, dy) and
    // level number (lx, ly) in the tileOffsets array and returns the
    // cooresponding offset.
    //
    
    long off;
    switch (ofd->tileDesc.mode)
    {
      case ONE_LEVEL:

	off = ofd->tileOffsets[0][dy][dx];
	break;

      case MIPMAP_LEVELS:

	off = ofd->tileOffsets[lx][dy][dx];
	break;

      case RIPMAP_LEVELS:

	off = ofd->tileOffsets[lx + ly*ofd->numXLevels][dy][dx];
	break;

      default:

	throw Iex::ArgExc ("Unknown LevelMode format.");
    }

    return off;
}


long
tileOffset(TiledOutputFile::Data const *ofd, int dx, int dy, int l)
{
    return tileOffset(ofd, dx, dy, l, l);
}


void
setTileOffset(TiledOutputFile::Data *ofd,
	      int dx, int dy, int lx, int ly, long offset)
{
    //
    // Looks up the tile with tile coordinate (dx, dy) and level number
    // (lx, ly) in the tileOffsets array and sets its value to offset.
    //

    switch (ofd->tileDesc.mode)
    {
      case ONE_LEVEL:

	ofd->tileOffsets[0][dy][dx] = offset;
	break;

      case MIPMAP_LEVELS:

	ofd->tileOffsets[lx][dy][dx] = offset;
	break;

      case RIPMAP_LEVELS:

	ofd->tileOffsets[lx + ly*ofd->numXLevels][dy][dx] = offset;
	break;

      default:

	throw Iex::ArgExc ("Unknown LevelMode format.");
    }
}


void
setTileOffset(TiledOutputFile::Data *ofd,
	      int dx, int dy, int l, long offset)
{
    setTileOffset(ofd, dx, dy, l, l, offset);
}


// done, i think
long
writeTileOffsets(TiledOutputFile::Data *ofd)
{
    //
    // Writes the tile index to the file. Returns the start position of the
    // index in the file.
    //
    
    long pos = ofd->os.tellp();

    if (pos == -1)
	Iex::throwErrnoExc ("Cannot determine current file position (%T).");

    switch (ofd->tileDesc.mode)
    {
      case ONE_LEVEL:
      case MIPMAP_LEVELS:
             
	// iterate over all levels
	for (size_t i_l = 0; i_l < ofd->tileOffsets.size(); ++i_l)
	{
	    // in this level, iterate over all y Tiles
	    for (size_t i_dy = 0;
		 i_dy < ofd->tileOffsets[i_l].size(); ++i_dy)
	    {
		// and iterate over all x Tiles
		for (size_t i_dx = 0;
		     i_dx < ofd->tileOffsets[i_l][i_dy].size(); ++i_dx)
		{
		    Xdr::write <StreamIO> (ofd->os,
					   ofd->tileOffsets[i_l][i_dy][i_dx]);
		}
	    }
	}
	break;
        
      case RIPMAP_LEVELS:

	// iterate over all lys
	for (size_t i_ly = 0; i_ly < ofd->numYLevels; ++i_ly)
	{
	    // iterate over all lxs
	    for (size_t i_lx = 0; i_lx < ofd->numXLevels; ++i_lx)
	    {
		int index = i_ly*ofd->numXLevels + i_lx;

		// and iterate over all y Tiles in level (i_lx, i_ly)
		for (size_t i_dy = 0;
		     i_dy < ofd->tileOffsets[index].size(); ++i_dy)
		{
		    // and iterate over all x Tiles
		    for (size_t i_dx = 0;
			 i_dx < ofd->tileOffsets[index][i_dy].size();
			 ++i_dx)
		    {
			Xdr::write <StreamIO> (ofd->os,
				ofd->tileOffsets[index][i_dy][i_dx]);
		    }
		}
	    }
	}
	break;
        
    default:

      throw Iex::ArgExc ("Unknown LevelMode format.");

    }

    return pos;
}

// FIXME, rename this, may go away if we use a 3d class for the offsets
void
resizeTileOffsets(TiledOutputFile::Data *ofd)
{
    switch (ofd->tileDesc.mode)
    {
      case ONE_LEVEL:
      case MIPMAP_LEVELS:

        ofd->tileOffsets.resize(ofd->numXLevels);

        // iterate over all levels
        for (size_t i_l = 0; i_l < ofd->tileOffsets.size(); ++i_l)
        {
	    ofd->tileOffsets[i_l].resize(ofd->numYTiles[i_l]);

	    // in this level, iterate over all x Tiles
	    for (size_t i_dx = 0;
		 i_dx < ofd->tileOffsets[i_l].size();
		 ++i_dx)
	    {
		ofd->tileOffsets[i_l][i_dx].resize(ofd->numXTiles[i_l],
						   long(0));
	    }
        }
        break;

      case RIPMAP_LEVELS:

        ofd->tileOffsets.resize(ofd->numXLevels * ofd->numYLevels);

        // iterate over all lys
        for (size_t i_ly = 0; i_ly < ofd->numYLevels; ++i_ly)
        {
	    // iterate over all lxs
	    for (size_t i_lx = 0; i_lx < ofd->numXLevels; ++i_lx)
	    {
		int index = i_ly * ofd->numXLevels + i_lx;
		ofd->tileOffsets[index].resize(ofd->numYTiles[i_ly]);

		// and iterate over all x Tiles in level (i_lx, i_ly)
		for (size_t i_dx = 0;
		     i_dx < ofd->tileOffsets[index].size();
		     ++i_dx)
		{
		    ofd->tileOffsets[index][i_dx].resize(ofd->numXTiles[i_lx],
							 long(0));
		}
	    }
	}
        break;

      default:

        throw Iex::ArgExc ("Unknown LevelMode format.");
    }
}


bool
tileOffsetsIsEmpty(TiledOutputFile::Data *ofd)
{
    switch (ofd->tileDesc.mode)
    {
      case ONE_LEVEL:
      case MIPMAP_LEVELS:
      
	// iterate over all levels
	for (size_t i_l = 0; i_l < ofd->tileOffsets.size(); ++i_l)
	{
	    // in this level, iterate over all y Tiles
	    for (size_t i_dy = 0;
		 i_dy < ofd->tileOffsets[i_l].size(); ++i_dy)
	    {
		// and iterate over all x Tiles
		for (size_t i_dx = 0;
		     i_dx < ofd->tileOffsets[i_l][i_dy].size(); ++i_dx)
		{
		    if (ofd->tileOffsets[i_l][i_dy][i_dx] != 0)
			return false;
		}
	    }
	}
	break;
        
      case RIPMAP_LEVELS:

	// iterate over all lys
	for (size_t i_ly = 0; i_ly < ofd->numYLevels; ++i_ly)
	{
	    // iterate over all lxs
	    for (size_t i_lx = 0; i_lx < ofd->numXLevels; ++i_lx)
	    {
		int index = i_ly*ofd->numXLevels + i_lx;

		// and iterate over all y Tiles in level (i_lx, i_ly)
		for (size_t i_dy = 0;
		     i_dy < ofd->tileOffsets[index].size(); ++i_dy)
		{
		    // and iterate over all x Tiles
		    for (size_t i_dx = 0;
			 i_dx < ofd->tileOffsets[index][i_dy].size();
			 ++i_dx)
		    {
			if (ofd->tileOffsets[index][i_dy][i_dx] != 0)
			    return false;
		    }
		}
	    }
	}
	break;
        
    default:

      throw Iex::ArgExc ("Unknown LevelMode format.");
      
      return true;

    }
    
    return true;
}


void
writeTileData (TiledOutputFile::Data *ofd,
               int dx, int dy, int lx, int ly, 
               const char pixelData[],
               int pixelDataSize)
{
    //
    // Store a block of pixel data in the output file, and try
    // to keep track of the current writing position the file,
    // without calling tellp() (tellp() can be fairly expensive).
    //

    long currentPosition = ofd->currentPosition;
    ofd->currentPosition = 0;

    if (currentPosition == 0)
        currentPosition = ofd->os.tellp();

    setTileOffset(ofd, dx, dy, lx, ly, currentPosition);

#ifdef DEBUG

    assert (long (ofd->os.tellp()) == currentPosition);

#endif

    //
    // Write the tile header.
    //
    Xdr::write <StreamIO> (ofd->os, dx);
    Xdr::write <StreamIO> (ofd->os, dy);
    Xdr::write <StreamIO> (ofd->os, lx);
    Xdr::write <StreamIO> (ofd->os, ly);
    Xdr::write <StreamIO> (ofd->os, pixelDataSize);

    ofd->os.write (pixelData, pixelDataSize);    
    checkError(ofd->os);

    //
    // Keep current position in the file so that we can avoid 
    // redundant seekg() operations (seekg() can be fairly expensive).
    //

    ofd->currentPosition = currentPosition +
                           5 * Xdr::size<int>() +
                           pixelDataSize;
}



void
bufferedTileWrite (TiledOutputFile::Data *ofd,
                   int dx, int dy, int lx, int ly, 
                   const char pixelData[],
                   int pixelDataSize)
{
    //
    // If tiles can be written randomly, then don't buffer anything.
    //
    
    if (ofd->lineOrder == RANDOM_Y)
    {
        writeTileData(ofd, dx, dy, lx, ly, pixelData, pixelDataSize);
        return;
    }
    
    
    //
    // If all the tiles before this one have already been written to the file,
    // then write this tile immediately and check if we have buffered tiles
    // that can be written after this tile.
    //
    // Otherwise, buffer the tile so it can be written to file later.
    //
    
    TileCoord currentTile = TileCoord(dx, dy, lx, ly);
    
    if (ofd->nextTileToWrite == currentTile)
    {
        writeTileData(ofd, dx, dy, lx, ly, pixelData, pixelDataSize);        
        ofd->nextTileToWrite = ofd->nextTileCoord(ofd->nextTileToWrite);        
        map<TileCoord, BufferedTile*>::iterator i =
                                ofd->tileMap.find(ofd->nextTileToWrite);
        
        //
        // Step through the tiles and write all successive buffered tiles after
        // the current one.
        //
        
        while(i != ofd->tileMap.end())
        {
            //
            // Write the tile, and then delete the tile's buffered data
            //

            writeTileData(ofd, i->first.dx, i->first.dy,
                               i->first.lx, i->first.ly,
                               i->second->pixelData,
                               i->second->pixelDataSize);

	    // FIXME, the destructor will take care of this
            delete [] i->second->pixelData;
            delete i->second;
            ofd->tileMap.erase(i);
            
            //
            // Proceed to the next tile
            //
            
            ofd->nextTileToWrite = ofd->nextTileCoord(ofd->nextTileToWrite);
            i = ofd->tileMap.find(ofd->nextTileToWrite);
        }
    }
    else
    {
        //
        // Create a new BufferedTile, copy the pixelData into it, and
        // insert it into the tileMap.
        //
        
        BufferedTile* bt = new BufferedTile();
        bt->pixelData = new char[pixelDataSize];

        // FIXME, move to constructor
        memcpy (bt->pixelData, (const char *) pixelData, pixelDataSize);
        bt->pixelDataSize = pixelDataSize;

        ofd->tileMap.insert(std::make_pair(currentTile, bt));
    }
}


void
convertToXdr (TiledOutputFile::Data *ofd, int numPixels, int inSize)
{
    //
    // Convert the contents of a TiledOutputFile's tileBuffer from the 
    // machine's native representation to Xdr format. This function is called
    // by writeTile(), below, if the compressor wanted its input pixel data
    // in the machine's native format, but then failed to compress the data
    // (most compressors will expand rather than compress random input data).
    //
    // Note that this routine assumes that the machine's native representation
    // of the pixel data has the same size as the Xdr representation.  This
    // makes it possible to convert the pixel data in place, without an
    // intermediate temporary buffer.
    //

    //
    // Set these to point to the start of the tile.
    // We will write to toPtr, and write from fromPtr.
    //
    char *toPtr = ofd->tileBuffer;
    char *fromPtr = toPtr;

    //
    // Iterate over all slices in the file.
    //

    for (unsigned int i = 0; i < ofd->slices.size(); ++i)
    {
	const OutSliceInfo &slice = ofd->slices[i];

	//
	// Convert the samples in place.
	//
            
	int i;
	switch (slice.type)
	{
	  case UINT:

	    for (i = 0; i < numPixels; ++i)
	    {
		Xdr::write <CharPtrIO>
		    (toPtr, *(const unsigned int *) fromPtr);
		fromPtr += sizeof(unsigned int);
	    }
	    break;

	  case HALF:

	    for (i = 0; i < numPixels; ++i)
	    {
		Xdr::write <CharPtrIO>
		    (toPtr, *(const half *) fromPtr);
		fromPtr += sizeof(half);
	    }
	    break;

	  case FLOAT:

	    for (i = 0; i < numPixels; ++i)
	    {
		Xdr::write <CharPtrIO>
		    (toPtr, *(const float *) fromPtr);
		fromPtr += sizeof(float);
	    }
	    break;

	  default:

	    throw Iex::ArgExc ("Unknown pixel data type.");
	}
    }

#ifdef DEBUG

    assert (toPtr == fromPtr);

#endif
}

} // namespace


TiledOutputFile::TiledOutputFile (const char fileName[], const Header &header):
    _data (new Data)
{
    try
    {
        header.sanityCheck(true);

        _data->header = header;
        _data->fileName = fileName;
        _data->lineOrder = _data->header.lineOrder();

        //
        // Check that the file is indeed tiled
        //

	// FIXME, use the new header::tileDescription() function
        _data->tileDesc = static_cast <TileDescriptionAttribute &>
                        (_data->header["tiles"]).value();

	// FIXME, move sampling checks to header::sanityCheck
        //
        // Ensure that xSampling and ySampling are 1 for all channels
        //

        const ChannelList &channels = _data->header.channels();
        for (ChannelList::ConstIterator i = channels.begin();
              i != channels.end(); ++i)
        {
            if (i.channel().xSampling != 1 || i.channel().ySampling != 1)
            {
                throw Iex::ArgExc ("All channels in a tiled file must have"
                           "sampling (1,1).");
            }
        }	

        //
        // Save the dataWindow information
        //

        const Box2i &dataWindow = _data->header.dataWindow();
        _data->minX = dataWindow.min.x;
        _data->maxX = dataWindow.max.x;
        _data->minY = dataWindow.min.y;
        _data->maxY = dataWindow.max.y;

        //
        // Precompute level and tile information to speed up utility functions
        //

	// FIXME, make this one function
        _data->precomputeNumXLevels();
        _data->precomputeNumYLevels();
        _data->precomputeNumXTiles();
        _data->precomputeNumYTiles();        
        
        //
        // Determine the first tile coordinate that we will be writing
        // if the file is not RANDOM_Y.
        //
        
        if (_data->lineOrder == INCREASING_Y)
        {
            _data->nextTileToWrite = TileCoord(0,0,0,0);
        }
        else if (_data->lineOrder == DECREASING_Y)
        {
            _data->nextTileToWrite = TileCoord(0,_data->numYTiles[0]-1,0,0);
        }

        _data->calculateMaxBytesPerLineForTile();

        _data->compressor = newTileCompressor (_data->header.compression(),
                               _data->maxBytesPerTileLine,
                               tileYSize(),
                               _data->header);

        _data->format = _data->compressor ? _data->compressor->format() :
                            Compressor::XDR;

        _data->tileBufferSize = _data->maxBytesPerTileLine * tileYSize();
        _data->tileBuffer.resizeErase (_data->tileBufferSize);

	// FIXME, not resize, but initialize
        resizeTileOffsets(_data);

#ifndef HAVE_IOS_BASE
        _data->os.open (fileName, std::ios::binary);        
#else
        _data->os.open (fileName, std::ios_base::binary);        
#endif

        if (!_data->os)
            Iex::throwErrnoExc();

	// FIXME, use isTiled flag
        _data->header.writeTo(_data->os, true);

        _data->tileOffsetsPosition = writeTileOffsets(_data);
        _data->currentPosition = _data->os.tellp();
    }
    catch (Iex::BaseExc &e)
    {
        delete _data;
        REPLACE_EXC (e, "Cannot open image file \"" << fileName << "\". " << e);
        throw;
    }
}


TiledOutputFile::~TiledOutputFile ()
{
    if (_data)
    {
        if (_data->tileOffsetsPosition >= 0)
        {
            try
            {
                _data->os.seekp(_data->tileOffsetsPosition);
                checkError(_data->os);
                writeTileOffsets(_data);
            }
            catch (...)
            {
                //
                // We cannot safely throw any exceptions from here.
                // This destructor may have been called because the
                // stack is currently being unwound for another
                // exception.
                //
            }
        }

        delete _data;
    }
}


const char *
TiledOutputFile::fileName () const
{
    return _data->fileName.c_str();
}


const Header &
TiledOutputFile::header () const
{
    return _data->header;
}


void	
TiledOutputFile::setFrameBuffer (const FrameBuffer &frameBuffer)
{
    //
    // Check if the new frame buffer descriptor
    // is compatible with the image file header.
    //

    const ChannelList &channels = _data->header.channels();

    for (ChannelList::ConstIterator i = channels.begin();
	 i != channels.end(); ++i)
    {
	FrameBuffer::ConstIterator j = frameBuffer.find (i.name());

	if (j == frameBuffer.end())
	    continue;

	if (i.channel().type != j.slice().type)
	{
	    THROW (Iex::ArgExc, "Pixel type of \"" << i.name() << "\" channel "
				"of output file \"" << fileName() << "\" is "
				"not compatible with the frame buffer's "
				"pixel type.");
	}

	if (j.slice().xSampling != 1 || j.slice().ySampling != 1)
	{
	    THROW (Iex::ArgExc, "All channels in a tiled file must have"
				"sampling (1,1).");
	}
    }
    
    //
    // Initialize slice table for writePixels().
    //

    vector<OutSliceInfo> slices;
    for (ChannelList::ConstIterator i = channels.begin();
	 i != channels.end(); ++i)
    {
	FrameBuffer::ConstIterator j = frameBuffer.find (i.name());

	if (j == frameBuffer.end())
	{
	    //
	    // Channel i is not present in the frame buffer.
	    // In the file, channel i will contain only zeroes.
	    //

	    slices.push_back (OutSliceInfo (i.channel().type,
					    0, // base
					    0, // xStride,
					    0, // yStride,
					    true)); // zero
	}
	else
	{
	    //
	    // Channel i is present in the frame buffer.
	    //

	    slices.push_back (OutSliceInfo (j.slice().type,
					    j.slice().base,
					    j.slice().xStride,
					    j.slice().yStride,
					    false)); // zero
	}
    }

    //
    // Store the new frame buffer.
    //

    _data->frameBuffer = frameBuffer;
    _data->slices = slices;
}


const FrameBuffer &
TiledOutputFile::frameBuffer () const
{
    return _data->frameBuffer;
}


void	
TiledOutputFile::writeTile (int dx, int dy, int lx, int ly)
{
    try
    {
	if (_data->slices.size() == 0)
	    throw Iex::ArgExc ("No frame buffer specified "
			       "as pixel data source.");

    if (!isValidTile(dx, dy, lx, ly))
        THROW (Iex::ArgExc, "Tried to write Tile (" << dx << "," << dy <<
                            "," << lx << "," << ly << "), but that is not a "
                            "valid tile coordinate.");
 
	if (tileOffset(_data, dx, dy, lx, ly) != 0)
        THROW (Iex::ArgExc, "Tried to write tile (" << dx << ", " << dy <<
                            ", " << lx << ", " << ly << ") more than once.");

	//
	// Convert one tile's worth of pixel data to
	// a machine-independent representation, and store
	// the result in _data->tileBuffer.
	//

	char *toPtr = _data->tileBuffer;

	Box2i tileRange = pixelRangeForTile(dx, dy, lx, ly);
	int numPixelsInTile = (tileRange.max.x - tileRange.min.x + 1) *
			      (tileRange.max.y - tileRange.min.y + 1);

	//
	// Iterate over all image channels.
	//

	for (unsigned int i = 0; i < _data->slices.size(); ++i)
	{
	    const OutSliceInfo &slice = _data->slices[i];

	    //
	    // Iterate over the sampled pixels.
	    //

	    if (slice.zero)
	    {
		//
		// The frame buffer contains no data for this channel.
		// Store zeroes in _data->tileBuffer.
		//

		if (_data->format == Compressor::XDR)
		{
		    //
		    // The compressor expects data in Xdr format.
		    //

		    int i;
		    switch (slice.type)
		    {
		      case UINT:

			for (i = 0; i < numPixelsInTile; ++i)
			    Xdr::write <CharPtrIO>
			    (toPtr, (unsigned int) 0);
			break;

		      case HALF:

			for (i = 0; i < numPixelsInTile; ++i)
			    Xdr::write <CharPtrIO> (toPtr, (half) 0);
			break;

		      case FLOAT:

			for (i = 0; i < numPixelsInTile; ++i)
			    Xdr::write <CharPtrIO> (toPtr, (float) 0);
			break;

		      default:

			throw Iex::ArgExc ("Unknown pixel data type.");
		    }
		}
		else
		{
		    //
		    // The compressor expects data in
		    // the machines native format.
		    //

                    int i;
		    switch (slice.type)
		    {
		      case UINT:

			for (i = 0; i < numPixelsInTile; ++i)
			{
			    static unsigned int ui = 0;
			    for (size_t i = 0; i < sizeof (ui); ++i)
				*toPtr++ = ((char *) &ui)[i];
			}
			break;

		      case HALF:

			for (i = 0; i < numPixelsInTile; ++i)
			{
			    *(half *) toPtr = half (0);
			    toPtr += sizeof (half);
			}
			break;

		      case FLOAT:

			for (i = 0; i < numPixelsInTile; ++i)
			{
			    static float f = 0;
			    for (size_t i = 0; i < sizeof (f); ++i)
				*toPtr++ = ((char *) &f)[i];
			}
			break;

		      default:

			throw Iex::ArgExc ("Unknown pixel data type.");
		    }
		}
	    }
	    else
	    {
		//
		// The frame buffer contains data for this channel.
		// If necessary, convert the pixel data to
		// a machine-independent representation,
		// and store in _data->tileBuffer.
		//

		const char *fromPtr;

		if (_data->format == Compressor::XDR)
		{
		    //
		    // The compressor expects data in Xdr format
		    //

                    int i;
		    switch (slice.type)
		    {
		      case UINT:

			for (i = tileRange.min.y;
			     i <= tileRange.max.y; ++i)
			{
			    fromPtr = slice.base +
				      i*slice.yStride +
				      tileRange.min.x*slice.xStride;

			    for (int j = tileRange.min.x;
				 j <= tileRange.max.x; ++j)
			    {
				Xdr::write <CharPtrIO>
				    (toPtr, *(unsigned int *) fromPtr);
				fromPtr += slice.xStride;
			    }
			}
			break;

		      case HALF:

			for (i = tileRange.min.y;
			     i <= tileRange.max.y; ++i)
			{
			    fromPtr = slice.base +
				      i*slice.yStride +
				      tileRange.min.x*slice.xStride;

			    for (int j = tileRange.min.x;
				 j <= tileRange.max.x; ++j)
			    {
				Xdr::write <CharPtrIO>
				    (toPtr, *(half *) fromPtr);
				fromPtr += slice.xStride;
			    }
			}
			break;

		      case FLOAT:

			for (i = tileRange.min.y;
			     i <= tileRange.max.y; ++i)
			{
			    fromPtr = slice.base +
				      i*slice.yStride +
				      tileRange.min.x*slice.xStride;

			    for (int j = tileRange.min.x;
				 j <= tileRange.max.x; ++j)
			    {
				Xdr::write <CharPtrIO>
				    (toPtr, *(float *) fromPtr);
				fromPtr += slice.xStride;
			    }
			}
			break;

		      default:

			throw Iex::ArgExc ("Unknown pixel data type.");
		    }
		}
		else
		{
		    //
		    // The compressor expects data in the
		    // machine's native format.
		    //

                    int i;
		    switch (slice.type)
		    {
		      case UINT:

			for (i = tileRange.min.y;
			     i <= tileRange.max.y; ++i)
			{
			    fromPtr = slice.base +
				      i*slice.yStride +
				      tileRange.min.x*slice.xStride;

			    for (int j = tileRange.min.x;
				 j <= tileRange.max.x; ++j)
			    {
				for (size_t i = 0; i < sizeof (unsigned int);
				     ++i)
				    *toPtr++ = fromPtr[i];

				fromPtr += slice.xStride;
			    }
			}
			break;

		      case HALF:

			for (i = tileRange.min.y;
			     i <= tileRange.max.y; ++i)
			{
			    fromPtr = slice.base +
				      i*slice.yStride +
				      tileRange.min.x*slice.xStride;

			    for (int j = tileRange.min.x;
				 j <= tileRange.max.x; ++j)
			    {
				*(half *)toPtr = *(half *)fromPtr;

				toPtr += sizeof (half);
				fromPtr += slice.xStride;
			    }
			}
			break;

		      case FLOAT:

			for (i = tileRange.min.y;
			     i <= tileRange.max.y; ++i)
			{
			    fromPtr = slice.base +
				      i*slice.yStride +
				      tileRange.min.x*slice.xStride;

    				for (int j = tileRange.min.x;
    				    j <= tileRange.max.x; ++j)
    				{
    				    for (size_t i = 0; i < sizeof (float); ++i)
    					*toPtr++ = fromPtr[i];

    				    fromPtr += slice.xStride;
    				}
			}
			break;

		      default:

			throw Iex::ArgExc ("Unknown pixel data type.");
		    }
		}
	    }
	}

	//
	// Compress the contents of _data->tileBuffer, 
	// and store the compressed data in the output file.
	//
        
	int dataSize = toPtr - _data->tileBuffer;
	const char *dataPtr = _data->tileBuffer;

	if (_data->compressor)
	{
	    const char *compPtr;
	    int compSize = _data->compressor->compressTile
			   (dataPtr, dataSize, tileRange, compPtr);

	    if (compSize < dataSize)
	    {
		dataSize = compSize;
		dataPtr = compPtr;
	    }
	    else if (_data->format == Compressor::NATIVE)
	    {
		//
		// The data did not shrink during compression, but
		// we cannot write to the file using NATIVE format,
		// so we need to convert the lineBuffer to XDR
		//

		convertToXdr(_data, numPixelsInTile, dataSize);

	    }
	}

	bufferedTileWrite (_data, dx, dy, lx, ly, dataPtr, dataSize);
    }
    catch (Iex::BaseExc &e)
    {
        REPLACE_EXC (e, "Failed to write pixel data to image "
                        "file \"" << fileName() << "\". " << e);
        throw;
    }
}

void
TiledOutputFile::writeTile (int dx, int dy, int l)
{
    writeTile(dx, dy, l, l);
}


void	
TiledOutputFile::copyPixels (TiledInputFile &in)
{
    //
    // Check if this file's and and the InputFile's
    // headers are compatible.
    //
    const Header &hdr = header();
    const Header &inHdr = in.header();

    const TileDescriptionAttribute *tileDesc = 0, *inTileDesc = 0;

    // FIXME, use Header::tileDescription()
    tileDesc = hdr.findTypedAttribute<TileDescriptionAttribute>("tiles");
    inTileDesc = inHdr.findTypedAttribute<TileDescriptionAttribute>("tiles");

    if(!tileDesc || !inTileDesc)
    {
        THROW (Iex::ArgExc, "Cannot copy pixels from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\". The output file "
               "tiled, but the input file is not. Try using "
               "OutputFile::copyPixels instead.");
    }

    // FIXME, make an operator == for TileDescription, and use that instead
    if(tileDesc->value().xSize != inTileDesc->value().xSize ||
       tileDesc->value().ySize != inTileDesc->value().ySize ||
       tileDesc->value().mode != inTileDesc->value().mode)
    {
        THROW (Iex::ArgExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed. "
               "The files have different tile descriptions.");
    }

    if (!(hdr.dataWindow() == inHdr.dataWindow()))
    {
        THROW (Iex::ArgExc, "Cannot copy pixels from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\". The files "
               "have different data windows.");
    }

    if (!(hdr.lineOrder() == inHdr.lineOrder()))
    {
        THROW (Iex::ArgExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed. "
               "The files have different line orders.");
    }

    if (!(hdr.compression() == inHdr.compression()))
    {
        THROW (Iex::ArgExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed. "
               "The files use different compression methods.");
    }

    if (!(hdr.channels() == inHdr.channels()))
    {
        THROW (Iex::ArgExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed.  "
               "The files have different channel lists.");
    }

    //
    // Verify that no pixel data have been written to this file yet.
    //

    if (!tileOffsetsIsEmpty(_data))
    {
        THROW (Iex::LogicExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed. "
               "\"" << fileName() << "\" already contains "
               "pixel data.");
    }

    //
    // Calculate the total number of tiles in the file
    //

    int numAllTiles = 0;
    switch (levelMode())
    {
      case ONE_LEVEL:
      case MIPMAP_LEVELS:

        for (size_t i_l = 0; i_l < numLevels(); ++i_l)
        {
            numAllTiles += numXTiles(i_l) * numYTiles(i_l);
        }
        break;

      case RIPMAP_LEVELS:

        for (size_t i_ly = 0; i_ly < numYLevels(); ++i_ly)
        {
            for (size_t i_lx = 0; i_lx < numXLevels(); ++i_lx)
            {
            numAllTiles += numXTiles(i_lx) * numYTiles(i_ly);
            }
        }
        break;

      default:

        throw Iex::ArgExc ("Unknown LevelMode format.");
    }

    for (int i = 0; i < numAllTiles; ++i)
    {
        const char *pixelData;
        int pixelDataSize, dx, dy, lx, ly;

        in.rawTileData (dx, dy, lx, ly, pixelData, pixelDataSize);
        writeTileData (_data, dx, dy, lx, ly, pixelData, pixelDataSize);
    }
}


// FIXME, call the function above
void	
TiledOutputFile::copyPixels (InputFile &in)
{
    //
    // Check if this file's and and the InputFile's
    // headers are compatible.
    //
    const Header &hdr = header();
    const Header &inHdr = in.header();

    const TileDescriptionAttribute *tileDesc = 0, *inTileDesc = 0;
    tileDesc = hdr.findTypedAttribute<TileDescriptionAttribute>("tiles");
    inTileDesc = inHdr.findTypedAttribute<TileDescriptionAttribute>("tiles");

    if(!tileDesc || !inTileDesc)
    {
        THROW (Iex::ArgExc, "Cannot copy pixels from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\". The output file "
               "tiled, but the input file is not. Try using "
               "OutputFile::copyPixels instead.");
    }

    if(tileDesc->value().xSize != inTileDesc->value().xSize ||
       tileDesc->value().ySize != inTileDesc->value().ySize ||
       tileDesc->value().mode != inTileDesc->value().mode)
    {
        THROW (Iex::ArgExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed. "
               "The files have different tile descriptions.");
    }

    if (!(hdr.dataWindow() == inHdr.dataWindow()))
    {
        THROW (Iex::ArgExc, "Cannot copy pixels from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\". The files "
               "have different data windows.");
    }

    if (!(hdr.lineOrder() == inHdr.lineOrder()))
    {
        THROW (Iex::ArgExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed. "
               "The files have different line orders.");
    }

    if (!(hdr.compression() == inHdr.compression()))
    {
        THROW (Iex::ArgExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed. "
               "The files use different compression methods.");
    }

    if (!(hdr.channels() == inHdr.channels()))
    {
        THROW (Iex::ArgExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed.  "
               "The files have different channel lists.");
    }

    //
    // Verify that no pixel data have been written to this file yet.
    //

    if (!tileOffsetsIsEmpty(_data))
    {
        THROW (Iex::LogicExc, "Quick pixel copy from image "
               "file \"" << in.fileName() << "\" to image "
               "file \"" << fileName() << "\" failed. "
               "\"" << fileName() << "\" already contains "
               "pixel data.");
    }

    //
    // Calculate the total number of tiles in the file
    //

    int numAllTiles = 0;
    switch (levelMode())
    {
      case ONE_LEVEL:
      case MIPMAP_LEVELS:

        for (size_t i_l = 0; i_l < numLevels(); ++i_l)
        {
            numAllTiles += numXTiles(i_l) * numYTiles(i_l);
        }
        break;

      case RIPMAP_LEVELS:

        for (size_t i_ly = 0; i_ly < numYLevels(); ++i_ly)
        {
            for (size_t i_lx = 0; i_lx < numXLevels(); ++i_lx)
            {
            numAllTiles += numXTiles(i_lx) * numYTiles(i_ly);
            }
        }
        break;

      default:

        throw Iex::ArgExc ("Unknown LevelMode format.");
    }

    for (int i = 0; i < numAllTiles; ++i)
    {
        const char *pixelData;
        int pixelDataSize, dx, dy, lx, ly;

        in.rawTileData (dx, dy, lx, ly, pixelData, pixelDataSize);
        writeTileData (_data, dx, dy, lx, ly, pixelData, pixelDataSize);
    }
}

// ------------------
// Utility functions
// ------------------

unsigned int
TiledOutputFile::tileXSize () const
{
    return _data->tileDesc.xSize;
}


unsigned int
TiledOutputFile::tileYSize () const
{
    return _data->tileDesc.ySize;
}


LevelMode
TiledOutputFile::levelMode () const
{
    return _data->tileDesc.mode;
}


int
TiledOutputFile::numLevels () const
{
    try
    {
	if (levelMode() == RIPMAP_LEVELS)
	    throw Iex::LogicExc ("numLevels not defined for RIPMAPs.");

	return _data->numXLevels;
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error calling numLevels() on image "
		     "file \"" << fileName() << "\". " << e);
	throw;
    }
}


int
TiledOutputFile::numXLevels () const
{
    return _data->numXLevels;
}


int
TiledOutputFile::numYLevels () const
{
    return _data->numYLevels;
}


int
TiledOutputFile::levelWidth (int lx) const
{
    try
    {
	if (lx < 0 || lx >= numXLevels())
	    throw Iex::ArgExc ("Parameter not in valid range.");

	// FIXME, take the max of this with 1
	return (_data->maxX - _data->minX + 1) / (1 << lx);
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error calling numXTiles() on image "
		     "file \"" << fileName() << "\". " << e);
	throw;
    }
}


int
TiledOutputFile::levelHeight (int ly) const
{
    try
    {
	if (ly < 0 || ly >= numYLevels())
	    throw Iex::ArgExc ("Parameter not in valid range.");

	// FIXME, take the max of this with 1
	return (_data->maxY - _data->minY + 1) / (1 << ly);
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error calling numXTiles() on image "
		     "file \"" << fileName() << "\". " << e);
	throw;
    }
}


int
TiledOutputFile::numXTiles (int lx) const
{
    try
    {
	if (lx < 0 || lx >= numXLevels())
	    throw Iex::ArgExc ("Parameter not in valid range.");

	return _data->numXTiles[lx];
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error calling numXTiles() on image "
		     "file \"" << fileName() << "\". " << e);
	throw;
    }
}


int
TiledOutputFile::numYTiles (int ly) const
{
    try
    {
	if (ly < 0 || ly >= numYLevels())
	    throw Iex::ArgExc ("Parameter not in valid range.");

	return _data->numYTiles[ly];
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error calling numYTiles() on image "
		     "file \"" << fileName() << "\". " << e);
	throw;
    }
}


// FIXME, rename to dataWindowForLevel
// fix comments refering to pixelRange and pixel range, etc
Box2i
TiledOutputFile::pixelRangeForLevel (int l) const
{
    return pixelRangeForLevel(l, l);
}


Box2i
TiledOutputFile::pixelRangeForLevel (int lx, int ly) const
{
    try
    {
	V2i levelMin = V2i(_data->minX, _data->minY);
	V2i levelMax = levelMin + V2i(levelWidth(lx) - 1, levelHeight(ly) -1);

	return Box2i(levelMin, levelMax);
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error calling pixelRangeForLevel() on image "
		     "file \"" << fileName() << "\". " << e);
	throw;
    }
}


// FIXME, rename to dataWindowForTile
// fix comments refering to pixelRange and pixel range, etc
Box2i
TiledOutputFile::pixelRangeForTile (int dx, int dy, int l) const
{
    return pixelRangeForTile(dx, dy, l, l);
}


Box2i
TiledOutputFile::pixelRangeForTile (int dx, int dy, int lx, int ly) const
{
    try
    {
	if (!isValidTile(dx,dy,lx,ly))
            THROW (Iex::ArgExc, "Tile (" << dx << "," << dy << "," << lx <<
            			"," << ly << ") is not a valid tile.");
	    
	V2i tileMin = V2i(_data->minX + dx * tileXSize(),
			  _data->minY + dy * tileYSize());
	V2i tileMax = tileMin + V2i(tileXSize() - 1, tileYSize() - 1);

	// FIXME, call dataWindowForLevel().max
	V2i levelMax = V2i(_data->minX, _data->minY) + V2i(levelWidth(lx) - 1, levelHeight(ly) -1);

#ifdef PLATFORM_WIN32
	tileMax = V2i(min(tileMax[0], levelMax[0]), min(tileMax[1], levelMax[1]));
#else
	tileMax = V2i(std::min(tileMax[0], levelMax[0]),
		      std::min(tileMax[1], levelMax[1]));
#endif

	return Box2i(tileMin, tileMax);
    }
    catch (Iex::BaseExc &e)
    {
	REPLACE_EXC (e, "Error calling pixelRangeForTile() on image "
		     "file \"" << fileName() << "\". " << e);
	throw;
    }
}


bool
TiledOutputFile::isValidTile(int dx, int dy, int lx, int ly) const
{
    return ((lx < numXLevels() && lx >= 0) &&
	    (ly < numYLevels() && ly >= 0) &&
	    (dx < numXTiles(lx) && dx >= 0) &&
	    (dy < numYTiles(ly) && dy >= 0));
}



} // namespace Imf
