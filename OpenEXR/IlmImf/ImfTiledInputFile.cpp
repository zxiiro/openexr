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
//	class TiledInputFile
//
//-----------------------------------------------------------------------------

#include <ImfTiledInputFile.h>
#include <ImfTileDescriptionAttribute.h>
#include <ImfChannelList.h>
#include <ImfMisc.h>
#include <ImfIO.h>
#include <ImfCompressor.h>
#include <ImathBox.h>
#include <ImfXdr.h>
#include <ImfArray.h>
#include <Iex.h>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <assert.h>
#include <ImathVec.h>
#include <ImfConvert.h>
#include <ImfVersion.h>

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
using std::ifstream;


namespace {

struct InSliceInfo
{
    PixelType   typeInFrameBuffer;
    PixelType   typeInFile;
    char *      base;
    size_t      xStride;
    size_t      yStride;
    bool        fill;
    bool        skip;
    double      fillValue;

    InSliceInfo (PixelType typeInFrameBuffer = HALF,
                 PixelType typeInFile = HALF,
                 char *base = 0,
                 size_t xStride = 0,
                 size_t yStride = 0,
                 bool fill = false,
                 bool skip = false,
                 double fillValue = 0.0);
};


InSliceInfo::InSliceInfo (PixelType tifb,
                          PixelType tifl,
                          char *b,
                          size_t xs, size_t ys,
                          bool f, bool s,
                          double fv)
:
    typeInFrameBuffer (tifb),
    typeInFile (tifl),
    base (b),
    xStride (xs),
    yStride (ys),
    fill (f),
    skip (s),
    fillValue (fv)
{
    // empty
}



} // namespace


// stores things that will be needed between calls to readTile
struct TiledInputFile::Data
{
public:
    string          fileName;               // the filename
    Header          header;                 // the image header
    TileDescription tileDesc;               // describes the tile layout
    int             version;                // file's version
    FrameBuffer     frameBuffer;            // framebuffer to write into
    LineOrder       lineOrder;              // the file's lineorder
    int             minX;                   // data window's min x coord
    int             maxX;                   // data window's max x coord
    int             minY;                   // data window's min y coord
    int             maxY;                   // data window's max x coord

    //
    // cached tile information:
    //
    int             numXLevels;             // number of x levels
    int             numYLevels;             // number of y levels
    int*            numXTiles;              // number of x tiles at a level
    int*            numYTiles;              // number of y tiles at a level

    // FIXME, make it a 3d or 4d class
    vector<vector<vector <long> > >
                    tileOffsets;            // stores offsets in file for
                                            // each tile

    long            currentPosition;        // file offset for current tile,
                                            // used to prevent unnecessary
                                            // seeking

    Compressor *    compressor;             // the compressor
    Compressor::Format  format;             // compressor's data format
    vector<InSliceInfo> slices;             // info about channels in file
    ifstream*       is;                     // file stream to read from

    size_t          maxBytesPerTileLine;    // combined size of a line
                                            // over all channels

    size_t          tileBufferSize;         // size of the tile buffer
    Array<char>     tileBuffer;             // holds a single tile
    const char *    uncompressedData;	    // the uncompressed tile

    bool            deleteStream;           // should we delete the stream
                                            // ourselves? or does someone else
                                            // do it?

    Data (bool del) :
        numXTiles(0), numYTiles(0),
        uncompressedData(0), compressor(0), deleteStream(del)
    {
        if (deleteStream)
            is = new ifstream();
    }

    ~Data ()
    {
        delete numXTiles;
        delete numYTiles;
        delete compressor;
        if (deleteStream)
            delete is;
    }

    //
    // Precomputation and caching of various tile parameters
    //

    // FIXME, these are in both the input and output tiled files, make a single place for them
    void		precomputeNumXLevels ();
    void		precomputeNumYLevels ();
    void		precomputeNumXTiles ();
    void		precomputeNumYTiles ();

    void calculateMaxBytesPerLineForTile();
};


//
// precomputation and caching of various tile parameters
//
void
TiledInputFile::Data::precomputeNumXLevels()
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
TiledInputFile::Data::precomputeNumYLevels()
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
TiledInputFile::Data::precomputeNumXTiles()
{
    delete numXTiles;
    numXTiles = new int[numXLevels];

    for (int i = 0; i < numXLevels; i++)
    {
        numXTiles[i] = ((maxX - minX + 1) / (int) pow(2, i) +
                        tileDesc.xSize - 1) / tileDesc.xSize;
    }
}


void
TiledInputFile::Data::precomputeNumYTiles()
{
    delete numYTiles;
    numYTiles = new int[numYLevels];

    for (int i = 0; i < numYLevels; i++)
    {
        numYTiles[i] = ((maxY - minY + 1) / (int) pow(2, i) +
                        tileDesc.ySize - 1) / tileDesc.ySize;
    }
}


// FIXME, move to common place
void
TiledInputFile::Data::calculateMaxBytesPerLineForTile()
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


//
// Looks up the value of the tile with tile coordinate (dx, dy) and
// level number (lx, ly) in the tileOffsets array and returns the
// cooresponding offset.
//
long
getTileOffset(TiledInputFile::Data const *ifd, int dx, int dy, int lx, int ly)
{
    long off;

    switch (ifd->tileDesc.mode)
    {
      case ONE_LEVEL:

        off = ifd->tileOffsets[0][dy][dx];
        break;

      case MIPMAP_LEVELS:

        off = ifd->tileOffsets[lx][dy][dx];
        break;

      case RIPMAP_LEVELS:

        off = ifd->tileOffsets[lx + ly*ifd->numXLevels][dy][dx];
        break;

      default:

        throw Iex::ArgExc ("Unknown LevelMode format.");
    }
    return off;
}


//
// Convenience function used for mipmaps.
//
long
getTileOffset(TiledInputFile::Data const *ifd, int dx, int dy, int l)
{
    return getTileOffset(ifd, dx, dy, l, l);
}


//
// Looks up the tile with tile coordinate (dx, dy) and level number 
// (lx, ly) in the tileOffsets array and sets its value to offset.
//
void
setTileOffset(TiledInputFile::Data *ifd, int dx, int dy, int lx, int ly,
              long offset)
{
    switch (ifd->tileDesc.mode)
    {
      case ONE_LEVEL:

        ifd->tileOffsets[0][dy][dx] = offset;
        break;

      case MIPMAP_LEVELS:

        ifd->tileOffsets[lx][dy][dx] = offset;
        break;

      case RIPMAP_LEVELS:

        ifd->tileOffsets[lx + ly*ifd->numXLevels][dy][dx] = offset;
        break;

      default:

        throw Iex::ArgExc ("Unknown LevelMode format.");
    }
}


//
// Convenience function used for mipmaps.
//
void
setTileOffset(TiledInputFile::Data *ifd, int dx, int dy, int l, long offset)
{
    setTileOffset(ifd, dx, dy, l, l, offset);
}


void
resizeTileOffsets(TiledInputFile::Data *ifd)
{
    switch (ifd->tileDesc.mode)
    {
      case ONE_LEVEL:
      case MIPMAP_LEVELS:

        ifd->tileOffsets.resize(ifd->numXLevels);

        // iterator over all levels
        for (size_t i_l = 0; i_l < ifd->tileOffsets.size(); ++i_l)
        {
            ifd->tileOffsets[i_l].resize(ifd->numYTiles[i_l]);

            // in this level, iterate over all y Tiles
            for (size_t i_dy = 0;
                 i_dy < ifd->tileOffsets[i_l].size();
                 ++i_dy)
            {
                ifd->tileOffsets[i_l][i_dy].resize(ifd->numXTiles[i_l]);
            }
        }
        break;

      case RIPMAP_LEVELS:

        ifd->tileOffsets.resize(ifd->numXLevels * ifd->numYLevels);

        // iterator over all ly's
        for (size_t i_ly = 0; i_ly < ifd->numYLevels; ++i_ly)
        {
            // iterator over all lx's
            for (size_t i_lx = 0; i_lx < ifd->numXLevels; ++i_lx)
            {
                int index = i_ly * ifd->numXLevels + i_lx;
                ifd->tileOffsets[index].resize(ifd->numYTiles[i_ly]);

                // and iterator over all y Tiles in level (i_lx, i_ly)
                for (size_t i_dy = 0;
                     i_dy < ifd->tileOffsets[index].size();
                     ++i_dy)
                {
                    ifd->tileOffsets[index][i_dy].resize(ifd->numXTiles[i_lx]);
                }
            }
        }
        break;

      default:

        throw Iex::ArgExc ("Unknown LevelMode format.");
    }
}


// done, i think
//
// The tile index stores the offset in the file for each tile. This is usually
// stored towards the beginning of the file. If the tile index is not complete
// (the file writing was aborted) this function will seek through the whole
// file, and reconstructs the tile index if possible.
//
void
reconstructTileOffsets (TiledInputFile::Data *ifd)
{
    long position = ifd->is->tellg();

    try
    {
        switch (ifd->tileDesc.mode)
        {
          case ONE_LEVEL:
          case MIPMAP_LEVELS:

            //
            // Seek to each tile and record its offset
            //

	    // FIXME, there is no need for all these for's, just calculate the number of expected
	    // tiles, and read their coordinates in.

            // iterator over all levels
            for (size_t i_l = 0; i_l < ifd->tileOffsets.size(); ++i_l)
            {
                // in this level, iterate over all y Tiles
                for (size_t i_dy = 0;
                     i_dy < ifd->tileOffsets[i_l].size(); ++i_dy)
                {
                    // and iterate over all x Tiles
                    for (size_t i_dx = 0;
                         i_dx < ifd->tileOffsets[i_l][i_dy].size(); ++i_dx)
                    {
                        if (!(*ifd->is))
                            break;

                        long tileOffset = ifd->is->tellg();

                        int tileX;
                        Xdr::read <StreamIO> (*ifd->is, tileX);

                        if (!(*ifd->is))
                            break;

                        int tileY;
                        Xdr::read <StreamIO> (*ifd->is, tileY);

                        if (!(*ifd->is))
                            break;

                        int levelX;
                        Xdr::read <StreamIO> (*ifd->is, levelX);

                        if (!(*ifd->is))
                            break;

                        int levelY;
                        Xdr::read <StreamIO> (*ifd->is, levelY);

                        if (!(*ifd->is))
                            break;

                        int dataSize;
                        Xdr::read <StreamIO> (*ifd->is, dataSize);

                        if (!(*ifd->is))
                            break;

			// FIXME, check IsValidTile()
                        Xdr::skip <StreamIO> (*ifd->is, dataSize);
                        setTileOffset(ifd, tileX, tileY, levelX, tileOffset);
                    }
                }
            }

            break;

          case RIPMAP_LEVELS:

            //
            // Seek to each tile and record its offset
            //

            // iterator over all ly's
            for (size_t i_ly = 0; i_ly < ifd->numYLevels; ++i_ly)
            {
                // iterator over all lx's
                for (size_t i_lx = 0; i_lx < ifd->numXLevels; ++i_lx)
                {
                    int index = i_ly*ifd->numXLevels + i_lx;

                    // and iterator over all y Tiles in level (i_lx, i_ly)
                    for (size_t i_dy = 0;
                         i_dy < ifd->tileOffsets[index].size(); ++i_dy)
                    {
                        // and iterate over all x Tiles
                        for (size_t i_dx = 0;
                             i_dx < ifd->tileOffsets[index][i_dy].size();
                             ++i_dx)
                        {
                            if (!(*ifd->is))
                                break;

                            long tileOffset = ifd->is->tellg();

                            int tileX;
                            Xdr::read <StreamIO> (*ifd->is, tileX);

                            if (!(*ifd->is))
                                break;

                            int tileY;
                            Xdr::read <StreamIO> (*ifd->is, tileY);

                            if (!(*ifd->is))
                                break;

                            int levelX;
                            Xdr::read <StreamIO> (*ifd->is, levelX);

                            if (!(*ifd->is))
                                break;

                            int levelY;
                            Xdr::read <StreamIO> (*ifd->is, levelY);

                            if (!(*ifd->is))
                                break;

                            int dataSize;
                            Xdr::read <StreamIO> (*ifd->is, dataSize);

                            if (!(*ifd->is))
                                break;

                            Xdr::skip <StreamIO> (*ifd->is, dataSize);

			    // FIXME, check IsValidTile()
                            setTileOffset(ifd, tileX, tileY, levelX, levelY,
                                          tileOffset);
                        }
                    }
                }
            }

            break;

         default:

	    // FIXME, assert
            ;// should not happen
         
        }
    }
    catch (...)
    {
        //
        // Suppress all exceptions.  This functions is
        // called only to reconstruct the line offset
        // table for incomplete files, and exceptions
        // are likely.
        //

	// TODO, add comments about the fact that we break out on the first exception
	// we don't try to recover tiles after the first exception.
    }

    ifd->is->clear();
    ifd->is->seekg (position);
}


// done, i think
//
// Reads the tile index from the file.
//
void
readTileOffsets (TiledInputFile::Data *ifd)
{
    switch (ifd->tileDesc.mode)
    {
      case ONE_LEVEL:
      case MIPMAP_LEVELS:

        {
            //
            // Read in the tile offsets from the file's index
            //
            // iterator over all levels
            for (size_t i_l = 0; i_l < ifd->tileOffsets.size(); ++i_l)
            {
                // in this level, iterate over all y Tiles
                for (size_t i_dy = 0;
                     i_dy < ifd->tileOffsets[i_l].size(); ++i_dy)
                {
                    // and iterate over all x Tiles
                    for (size_t i_dx = 0;
                         i_dx < ifd->tileOffsets[i_l][i_dy].size(); ++i_dx)
                    {
                        Xdr::read <StreamIO> (*ifd->is,
                                ifd->tileOffsets[i_l][i_dy][i_dx]);
                    }
                }
            }

            //
            // Error check the tile offsets
            //
            int error = false;

            // iterator over all levels
            for (size_t i_l = 0; i_l < ifd->tileOffsets.size() && !error;
                 ++i_l)
            {
                // in this level, iterate over all y Tiles
                for (size_t i_dy = 0;
                     i_dy < ifd->tileOffsets[i_l].size() && !error; ++i_dy)
                {
                    // and iterate over all x Tiles
                    for (size_t i_dx = 0;
                         i_dx < ifd->tileOffsets[i_l][i_dy].size() && !error;
                         ++i_dx)
                    {
                        if (ifd->tileOffsets[i_l][i_dy][i_dx] <= 0)
                        {
                            //
                            // Invalid data in the line offset table mean that
                            // the file is probably incomplete (the table is
                            // the last thing written to the file).  Either
                            // some process is still busy writing the file,
                            // or writing the file was aborted.
                            //
                            // We should still be able to read the existing
                            // parts of the file.  In order to do this, we
                            // have to make a sequential scan over the scan
                            // line data to reconstruct the line offset table.
                            //

                            error = true;
                            break;
                        }
                    }
                }
            }

            if (error)
                reconstructTileOffsets (ifd);
        }
        break;

      case RIPMAP_LEVELS:

        {
            //
            // Read in the tile offsets from the file's index
            //

            // iterator over all ly's
            for (size_t i_ly = 0; i_ly < ifd->numYLevels; ++i_ly)
            {
                // iterator over all lx's
                for (size_t i_lx = 0; i_lx < ifd->numXLevels; ++i_lx)
                {
                    int index = i_ly*ifd->numXLevels + i_lx;

                    // and iterator over all y Tiles in level (i_lx, i_ly)
                    for (size_t i_dy = 0;
                         i_dy < ifd->tileOffsets[index].size(); ++i_dy)
                    {
                        // and iterate over all x Tiles
                        for (size_t i_dx = 0;
                             i_dx < ifd->tileOffsets[index][i_dy].size(); ++i_dx)
                        {
                            Xdr::read <StreamIO> (*ifd->is,
                                ifd->tileOffsets[index][i_dy][i_dx]);
                        }
                    }
                }
            }

            //
            // Error check the tile offsets
            //
            int error = false;

	    // FIXME, add a function to the 3d tileoffset class that checks for errors, and use that
            // iterator over all ly's
            for (size_t i_ly = 0; i_ly < ifd->numYLevels && !error; ++i_ly)
            {
                // iterator over all lx's
                for (size_t i_lx = 0; i_lx < ifd->numXLevels && !error; ++i_lx)
                {
                    int index = i_ly*ifd->numXLevels + i_lx;

                    // and iterator over all y Tiles in level (i_lx, i_ly)
                    for (size_t i_dy = 0;
                         i_dy < ifd->tileOffsets[index].size() && !error;
                         ++i_dy)
                    {
                        // and iterate over all x Tiles
                        for (size_t i_dx = 0;
                             i_dx < ifd->tileOffsets[index][i_dy].size() && !error;
                             ++i_dx)
                        {
                            if (ifd->tileOffsets[index][i_dy][i_dx] <= 0)
                            {
                                //
                                // Invalid data in the line offset table mean that
                                // the file is probably incomplete (the table is
                                // the last thing written to the file).  Either
                                // some process is still busy writing the file,
                                // or writing the file was aborted.
                                //
                                // We should still be able to read the existing
                                // parts of the file.  In order to do this, we
                                // have to make a sequential scan over the scan
                                // line data to reconstruct the line offset table.
                                //

                                error = true;
                                break;
                            }
                        }
                    }
                }
            }

            if (error)
                reconstructTileOffsets (ifd);
        }
        break;

     default:

        throw Iex::ArgExc ("Unknown LevelMode format.");
    }
}


// done, i think
//
// Reads a single tile block from the file
//
void
readTileData (TiledInputFile::Data *ifd, int dx, int dy, int lx, int ly,
              int &dataSize)
{
    //
    // Look up the location for this tile in the Index and
    // seek to that position if necessary
    //
    
    long tileOffset = getTileOffset(ifd, dx, dy, lx, ly);

    if (tileOffset == 0)
        THROW (Iex::InputExc, "Tile (" << dx << "," << dy << "," << lx <<
                              "," << ly << ") is missing.");
    //
    // Seek to the start of the scan line in the file,
    // if necessary.
    //
    
    if (ifd->currentPosition != tileOffset)
    {
        ifd->is->seekg (tileOffset);
        checkError (*ifd->is);
    }

#ifdef DEBUG

    assert (long (ifd->is->tellg()) == tileOffset);

#endif

    //
    // Read the first few bytes of the tile (the header).
    // Test that the tile coords and the level number are
    // correct.
    //
    
    int tileXCoord, tileYCoord, levelX, levelY;
    Xdr::read <StreamIO> (*ifd->is, tileXCoord);
    Xdr::read <StreamIO> (*ifd->is, tileYCoord);
    Xdr::read <StreamIO> (*ifd->is, levelX);
    Xdr::read <StreamIO> (*ifd->is, levelY);
    Xdr::read <StreamIO> (*ifd->is, dataSize);

    if (tileXCoord != dx)
        throw Iex::InputExc ("Unexpected tile x coordinate.");

    if (tileYCoord != dy)
        throw Iex::InputExc ("Unexpected tile y coordinate.");

    if (levelX != lx)
        throw Iex::InputExc ("Unexpected tile x level number coordinate.");

    if (levelY != ly)
        throw Iex::InputExc ("Unexpected tile y level number coordinate.");

    if (dataSize > (int) ifd->tileBufferSize)
        throw Iex::InputExc ("Unexpected tile block length.");

    //
    // Read the pixel data.
    //

    ifd->is->read (ifd->tileBuffer, dataSize);
    checkError (*ifd->is);

    //
    // Keep track of which tile is the next one in
    // the file, so that we can avoid redundant seekg()
    // operations (seekg() can be fairly expensive).
    //
    
    ifd->currentPosition = tileOffset +
                           5*Xdr::size<int>() +
			   dataSize;
}

// done, i think
//
// Reads the next tile block from the file.
//
void
readNextTileData (TiledInputFile::Data *ifd, int &dx, int &dy, int &lx,
                  int &ly, int &dataSize)
{
    //
    // Read the first few bytes of the tile (the header).
    //

    Xdr::read <StreamIO> (*ifd->is, dx);
    Xdr::read <StreamIO> (*ifd->is, dy);
    Xdr::read <StreamIO> (*ifd->is, lx);
    Xdr::read <StreamIO> (*ifd->is, ly);
    Xdr::read <StreamIO> (*ifd->is, dataSize);

    if (dataSize > (int) ifd->tileBufferSize)
        throw Iex::InputExc ("Unexpected tile block length.");
    
    //
    // Read the pixel data.
    //

    ifd->is->read (ifd->tileBuffer, dataSize);
    checkError (*ifd->is);
    
    //
    // Keep track of which tile is the next one in
    // the file, so that we can avoid redundant seekg()
    // operations (seekg() can be fairly expensive).
    //

    ifd->currentPosition += 5 * Xdr::size<int>() + dataSize;
}


} // namespace


// done, i think
//
// Constructs a new TiledInputFile.
// This constructor is used externally when a user wants to read in a tiled
// file explicitely.
//
TiledInputFile::TiledInputFile (const char fileName[]) :
    _data (new Data(true))
{
    try
    {
        _data->fileName = fileName;
#ifndef HAVE_IOS_BASE
        _data->is->open (fileName, std::ios::binary|std::ios::in);
#else
        _data->is->open (fileName, std::ios_base::binary);
#endif

        if (!(*_data->is))
        Iex::throwErrnoExc();

        _data->header.readFrom (*_data->is, _data->version);
        _data->header.sanityCheck(true);

        if (!isTiled(_data->version))
            throw Iex::ArgExc("Input file doesn't appear to be a tiled file. "
                              "Incorrect file version flag.");

	// FIXME, use Header::tileDescription()
        _data->tileDesc = static_cast <TileDescriptionAttribute &>
                            (_data->header["tiles"]).value();    

        _data->lineOrder = _data->header.lineOrder();

        const Box2i &dataWindow = _data->header.dataWindow();

        _data->minX = dataWindow.min.x;
        _data->maxX = dataWindow.max.x;
        _data->minY = dataWindow.min.y;
        _data->maxY = dataWindow.max.y;

	// FIXME, use common precomputation functions from e.g. ImfTiledMisc.h
        _data->precomputeNumXLevels();
        _data->precomputeNumYLevels();
        _data->precomputeNumXTiles();
        _data->precomputeNumYTiles();

        _data->calculateMaxBytesPerLineForTile();

        _data->compressor = newTileCompressor (_data->header.compression(),
                           _data->maxBytesPerTileLine,
                           tileYSize(),
                           _data->header);

        _data->format = _data->compressor?
                _data->compressor->format(): Compressor::XDR;

        _data->tileBufferSize = _data->maxBytesPerTileLine * tileYSize();
        _data->tileBuffer.resizeErase (_data->tileBufferSize);

        _data->uncompressedData = 0;

        resizeTileOffsets(_data);
        readTileOffsets(_data);

        _data->currentPosition = _data->is->tellg();
    }
    catch (Iex::BaseExc &e)
    {
        delete _data;
        REPLACE_EXC (e, "Cannot open image file \"" << fileName << "\". " << e);
        throw;
    }
}


// done, i think
//
// Constructs a new TiledInputFile.
// This constructor is only used internally by InputFile when a user wants to
// just read in a file and doesn't care or know if the file is tiled.
//
TiledInputFile::TiledInputFile (const char fileName[],
                                const Header &header,
                                std::ifstream &is) :
    _data (new Data(false))
{
    _data->fileName = fileName;

    _data->is = &is;
    if (!(*_data->is))
        Iex::throwErrnoExc();

    _data->header = header;

    // FIXME, use Header::tileDescription()
    _data->tileDesc = static_cast <TileDescriptionAttribute &>
                        (_data->header["tiles"]).value();    

    _data->lineOrder = _data->header.lineOrder();

    const Box2i &dataWindow = _data->header.dataWindow();

    _data->minX = dataWindow.min.x;
    _data->maxX = dataWindow.max.x;
    _data->minY = dataWindow.min.y;
    _data->maxY = dataWindow.max.y;

    // FIXME, use common functions
    _data->precomputeNumXLevels();
    _data->precomputeNumYLevels();
    _data->precomputeNumXTiles();
    _data->precomputeNumYTiles();

    _data->calculateMaxBytesPerLineForTile();

    _data->compressor = newTileCompressor(_data->header.compression(),
                                          _data->maxBytesPerTileLine,
                                          tileYSize(),
                                          _data->header);

    _data->format = _data->compressor ? _data->compressor->format()
                                      : Compressor::XDR;

    _data->tileBufferSize = _data->maxBytesPerTileLine * tileYSize();
    _data->tileBuffer.resizeErase (_data->tileBufferSize);

    _data->uncompressedData = 0;

    resizeTileOffsets(_data);
    readTileOffsets(_data);

    _data->currentPosition = _data->is->tellg();
}


//
// deletes the file and any associated data that was created for it.
//
TiledInputFile::~TiledInputFile ()
{
    delete _data;
}


//
// Access to the file name
//
const char *
TiledInputFile::fileName () const
{
    return _data->fileName.c_str();
}


//
// Access to the file header
//
const Header &
TiledInputFile::header () const
{
    return _data->header;
}


//
// Access to the file format version
//
int
TiledInputFile::version () const
{
    return _data->version;
}


//
// Set the frame buffer
//
void	
TiledInputFile::setFrameBuffer (const FrameBuffer &frameBuffer)
{
    //
    // Check if the new frame buffer descriptor is
    // compatible with the image file header.
    //

    const ChannelList &channels = _data->header.channels();

    for (FrameBuffer::ConstIterator j = frameBuffer.begin();
         j != frameBuffer.end();
         ++j)
    {
        ChannelList::ConstIterator i = channels.find (j.name());

        if (i == channels.end())
            continue;

        if (i.channel().type != j.slice().type)
        {
            THROW (Iex::ArgExc, "Pixel type of \"" << i.name() << "\" channel "
                        "of input file \"" << fileName() << "\" is "
                        "not compatible with the frame buffer's "
                        "pixel type.");
        }

        if (i.channel().xSampling != j.slice().xSampling ||
            i.channel().ySampling != j.slice().ySampling)
        {
            THROW (Iex::ArgExc, "X and/or y subsampling factors "
                    "of \"" << i.name() << "\" channel "
                    "of input file \"" << fileName() << "\" are "
                    "not compatible with the frame buffer's "
                    "subsampling factors.");
        }
    }

    //
    // Initialize the slice table for readPixels().
    //

    vector<InSliceInfo> slices;
    ChannelList::ConstIterator i = channels.begin();

    for (FrameBuffer::ConstIterator j = frameBuffer.begin();
         j != frameBuffer.end();
         ++j)
    {
        while (i != channels.end() && strcmp (i.name(), j.name()) < 0)
        {
            //
            // Channel i is present in the file but not
            // in the frame buffer; data for channel i
            // will be skipped during readPixels().
            //

            slices.push_back (InSliceInfo (i.channel().type,
                           i.channel().type,
                           0, // base
                           0, // xStride
                           0, // yStride
                           false,  // fill
                           true, // skip
                           0.0)); // fillValue
            ++i;
        }

        bool fill = false;

        if (i == channels.end() || strcmp (i.name(), j.name()) > 0)
        {
            //
            // Channel i is present in the frame buffer, but not in the file.
            // In the frame buffer, slice j will be filled with a default value.
            //

            fill = true;
        }

        slices.push_back (InSliceInfo (j.slice().type,
                                       fill ? j.slice().type
                                            : i.channel().type,
                                       j.slice().base,
                                       j.slice().xStride,
                                       j.slice().yStride,
                                       fill,
                                       false, // skip
                                       j.slice().fillValue));

        if (i != channels.end() && !fill)
            ++i;
    }

    //
    // Store the new frame buffer.
    //

    _data->frameBuffer = frameBuffer;
    _data->slices = slices;
}


//
// Access to the frame buffer
//
const FrameBuffer &
TiledInputFile::frameBuffer () const
{
    return _data->frameBuffer;
}


//
// Read a tile from the file into the framebuffer
//
void	
TiledInputFile::readTile (int dx, int dy, int lx, int ly)
{
    try
    {    
        if (_data->slices.size() == 0)
            throw Iex::ArgExc ("No frame buffer specified "
                       "as pixel data source.");

        if (!isValidTile(dx, dy, lx, ly))
        {
            THROW (Iex::ArgExc, "Tile (" << dx << "," << dy << "," <<
                    lx << "," << ly << ") is not a valid "
                    "tile.");
        }

        // calculate information about the tile
	// FIXME, dataWindowForTile
        Box2i tileRange = pixelRangeForTile(dx, dy, lx, ly);
        int numPixelsInTile = (tileRange.max.x - tileRange.min.x + 1) *
                              (tileRange.max.y - tileRange.min.y + 1);

        //
        // Used to force the lineBuffer to be interpreted as XDR.
        // This is needed because PIZ can store in NATIVE format,
        // but when a lineBuffer is not compressed, it has to be
        // saved in XDR format for it works across machines with
        // different byte orders
        //

        bool forceXdr = false;

        //
        // Read the data block for this tile into _data->tileBuffer
        //

        int dataSize;
        readTileData (_data, dx, dy, lx, ly, dataSize);
        forceXdr = false;

        int sizeOfTile = 0;
        for (unsigned int i = 0; i < _data->slices.size(); ++i)
        {
            sizeOfTile += pixelTypeSize(_data->slices[i].typeInFile) * numPixelsInTile;
        }

        //
        // Uncompress the data.
        //

        if (_data->compressor && dataSize < sizeOfTile)
        {
            dataSize = _data->compressor->uncompressTile
                (_data->tileBuffer, dataSize, tileRange,
                _data->uncompressedData);
        }
        else
        {
            // If the line is uncompressed, but the compressor
            // says that its in NATIVE format, don't believe it.

            if (_data->format != Compressor::XDR)
                forceXdr = true;
            _data->uncompressedData = _data->tileBuffer;
        }

        //
        // Convert the tile of pixel data back
        // from the machine-independent representation, and
        // store the result in the frame buffer.
        //

        //
        // Iterate over all image channels.
        //
        
        // points to where we read from in the tile block
        const char *readPtr = _data->uncompressedData;    
        
        for (unsigned int i = 0; i < _data->slices.size(); ++i)
        {
            const InSliceInfo &slice = _data->slices[i];

            //
            // Iterate over the sampled pixels.
            //

            if (slice.skip)
            {
                //
                // The file contains data for this channel, but
                // the frame buffer contains no slice for this channel.
                //

                switch (slice.typeInFile)
                {
                  case UINT:

                    Xdr::skip <CharPtrIO>
                        (readPtr, Xdr::size <unsigned int> () *
                         (numPixelsInTile));
                  break;

                  case HALF:

                    Xdr::skip <CharPtrIO>
                        (readPtr, Xdr::size <half> () * (numPixelsInTile));
                  break;

                  case FLOAT:

                    Xdr::skip <CharPtrIO>
                        (readPtr, Xdr::size <float> () * (numPixelsInTile));
                  break;

                  default:

                    throw Iex::ArgExc ("Unknown pixel data type.");
                }
            }
            else
            {
                //
                // The frame buffer contains a slice for this channel.
                //

                // points to where we write to in the framebuffer
                char *pixelPtr;

                if (slice.fill)
                {
                    //
                    // The file contains no data for this channel.
                    // Store a default value in the frame buffer.
                    //

                    switch (slice.typeInFrameBuffer)
                    {
                      case UINT:

                      {
                        unsigned int fillValue =
                        (unsigned int) (slice.fillValue);

                        for (int i = tileRange.min.y;
                             i <= tileRange.max.y; ++i)
                        {
                            pixelPtr = slice.base +
                                   i*slice.yStride +
                                   tileRange.min.x*slice.xStride;
                            for (int j = tileRange.min.x;
                                 j <= tileRange.max.x; ++j)
                            {
                                *(unsigned int *) pixelPtr = fillValue;
                                pixelPtr += slice.xStride;
                            }
                        }
                      }
                      break;

                      case HALF:

                      {
                        half fillValue =
                        half (slice.fillValue);

                        for (int i = tileRange.min.y;
                             i <= tileRange.max.y; ++i)
                        {
                            pixelPtr = slice.base +
                                       i*slice.yStride +
                                       tileRange.min.x*slice.xStride;

                            for (int j = tileRange.min.x;
                                 j <= tileRange.max.x; ++j)
                            {
                                *(half *) pixelPtr = fillValue;
                                pixelPtr += slice.xStride;
                            }
                        }
                      }
                      break;

                      case FLOAT:

                      {
                        float fillValue =
                        float (slice.fillValue);

                        for (int i = tileRange.min.y;
                             i <= tileRange.max.y; ++i)
                        {
                            pixelPtr = slice.base +
                                       i*slice.yStride +
                                       tileRange.min.x*slice.xStride;

                            for (int j = tileRange.min.x;
                                 j <= tileRange.max.x; ++j)
                            {
                                *(float *) pixelPtr = fillValue;
                                pixelPtr += slice.xStride;
                            }
                        }
                      }
                      break;

                      default:

                        throw Iex::ArgExc ("Unknown pixel data type.");
                    }
                }
                else if (_data->format == Compressor::XDR || forceXdr)
                {
                    //
                    // The compressor produced data for this
                    // channel in Xdr format.
                    //
                    // Convert the pixels from the file's machine-
                    // independent representation, and store the
                    // results the frame buffer.
                    //

                    switch (slice.typeInFrameBuffer)
                    {
                      case UINT:
                      
                        switch (slice.typeInFile)
                        {
                          case UINT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    Xdr::read <CharPtrIO>
                                    (readPtr, *(unsigned int *) pixelPtr);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case HALF:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    half h;
                                    Xdr::read <CharPtrIO> (readPtr, h);
                                    *(unsigned int *) pixelPtr =
                                            halfToUint (h);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case FLOAT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    float f;
                                    Xdr::read <CharPtrIO> (readPtr, f);
                                    *(unsigned int *) pixelPtr =
                                            floatToUint (f);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;
                        }
                        break;

                      case HALF:
                        
                        switch (slice.typeInFile)
                        {
                          case UINT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    unsigned int ui;
                                    Xdr::read <CharPtrIO> (readPtr, ui);
                                    *(half *) pixelPtr = uintToHalf (ui);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case HALF:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    Xdr::read <CharPtrIO>
                                        (readPtr, *(half *) pixelPtr);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case FLOAT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    float f;
                                    Xdr::read <CharPtrIO> (readPtr, f);
                                    *(half *) pixelPtr = floatToHalf (f);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;
                        }
                        break;

                      case FLOAT:

                        switch (slice.typeInFile)
                        {
                          case UINT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    unsigned int ui;
                                    Xdr::read <CharPtrIO> (readPtr, ui);
                                    *(float *) pixelPtr = float (ui);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case HALF:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    half h;
                                    Xdr::read <CharPtrIO> (readPtr, h);
                                    *(float *) pixelPtr = float (h);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case FLOAT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    Xdr::read <CharPtrIO>
                                    (readPtr, *(float *) pixelPtr);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;
                        }
                        break;

                      default:

                        throw Iex::ArgExc ("Unknown pixel data type.");
                    }
                }
                else
                {
                    //
                    // The compressor produced data for this
                    // channel in the machine's native format.
                    //
                    // Convert the pixels from the file's machine-
                    // dependent representation, and store the
                    // results the frame buffer.
                    //

                    switch (slice.typeInFrameBuffer)
                    {
                      case UINT:
                      
                        switch (slice.typeInFile)
                        {
                          case UINT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    for (size_t i = 0;
                                         i < sizeof (unsigned int); ++i)
                                        pixelPtr[i] = readPtr[i];

                                    readPtr += sizeof (unsigned int);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case HALF:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    half h = *(half *) readPtr;
                                    *(unsigned int *) pixelPtr = halfToUint (h);
                                    readPtr += sizeof (half);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case FLOAT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    float f;

                                    for (size_t i = 0; i < sizeof (float); ++i)
                                    {
                                        ((char *)&f)[i] = readPtr[i];
                                    }

                                    *(unsigned int *)pixelPtr = floatToUint (f);
                                    readPtr += sizeof (float);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;
                        }
                        break;

                      case HALF:

                        switch (slice.typeInFile)
                        {
                          case UINT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    unsigned int ui;

                                    for (size_t i = 0;
                                         i < sizeof (unsigned int);
                                         ++i)
                                    {
                                        ((char *)&ui)[i] = readPtr[i];
                                    }

                                    *(half *) pixelPtr = uintToHalf (ui);
                                    readPtr += sizeof (unsigned int);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case HALF:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    *(half *) pixelPtr = *(half *)readPtr;

                                    readPtr += sizeof (half);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case FLOAT:

                            for (int i = tileRange.min.y;
                                 i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    float f;

                                    for (size_t i = 0; i < sizeof (float); ++i)
                                    {
                                        ((char *)&f)[i] = readPtr[i];
                                    }

                                    *(half *) pixelPtr = floatToHalf (f);
                                    readPtr += sizeof (float);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;
                        }
                        break;

                      case FLOAT:

                        switch (slice.typeInFile)
                        {
                          case UINT:

                            for (int i = tileRange.min.y;
                                i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    unsigned int ui;

                                    for (size_t i = 0;
                                         i < sizeof (unsigned int);
                                         ++i)
                                    {
                                        ((char *)&ui)[i] = readPtr[i];
                                    }

                                    *(float *) pixelPtr = float (ui);
                                    readPtr += sizeof (unsigned int);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case HALF:

                            for (int i = tileRange.min.y;
                                i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    half h = *(half *) readPtr;
                                    *(float *) pixelPtr = float (h);
                                    readPtr += sizeof (half);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;

                          case FLOAT:

                            for (int i = tileRange.min.y;
                                i <= tileRange.max.y; ++i)
                            {
                                pixelPtr = slice.base +
                                i*slice.yStride +
                                tileRange.min.x*slice.xStride;

                                for (int j = tileRange.min.x;
                                     j <= tileRange.max.x; ++j)
                                {
                                    for (size_t i = 0; i < sizeof (float); ++i)
                                        pixelPtr[i] = readPtr[i];

                                    readPtr += sizeof (float);
                                    pixelPtr += slice.xStride;
                                }
                            }
                            break;
                        }
                        break;

                      default:

                        throw Iex::ArgExc ("Unknown pixel data type.");
                    }
                }
            }
        }
    }
    catch (Iex::BaseExc &e)
    {
        REPLACE_EXC (e, "Error reading pixel data from image "
                        "file \"" << fileName() << "\". " << e);
        throw;
    }
}

// done
//
// Convenience function used for mipmaps
//
void	
TiledInputFile::readTile (int dx, int dy, int l)
{
    readTile (dx, dy, l, l);
}


// done, i think
//
// Read a block of raw pixel data from the file,
// without uncompressing it (this function is
// used to implement OutputFile::copyPixels()).
//
void
TiledInputFile::rawTileData (int & dx, int & dy, int & lx, int & ly,
                             const char *&pixelData, int &pixelDataSize)
{
    try
    {
        if (!isValidTile(dx, dy, lx, ly))
        {
            throw Iex::ArgExc ("Tried to read a tile outside "
                       "the image file's data window.");
        }

        readNextTileData (_data, dx, dy, lx, ly, pixelDataSize);
        pixelData = _data->tileBuffer;
    }
    catch (Iex::BaseExc &e)
    {
        REPLACE_EXC (e, "Error reading pixel data from image "
                    "file \"" << fileName() << "\". " << e);
        throw;
    }
}


// ------------------
// Utility functions
// ------------------

unsigned int
TiledInputFile::tileXSize () const
{
    return _data->tileDesc.xSize;
}


unsigned int
TiledInputFile::tileYSize () const
{
    return _data->tileDesc.ySize;
}


LevelMode
TiledInputFile::levelMode () const
{
    return _data->tileDesc.mode;
}


int
TiledInputFile::numLevels () const
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

        return 0;
    }
}


int
TiledInputFile::numXLevels () const
{
    return _data->numXLevels;
}


int
TiledInputFile::numYLevels () const
{
    return _data->numYLevels;
}


int
TiledInputFile::levelWidth (int lx) const
{
    return (_data->maxX - _data->minX + 1) / (int)pow(2, lx);
}


int
TiledInputFile::levelHeight (int ly) const
{
    return (_data->maxY - _data->minY + 1) / (int)pow(2, ly);
}


int
TiledInputFile::numXTiles (int lx) const
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

        return 0;
    }
}


int
TiledInputFile::numYTiles (int ly) const
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

        return 0;
    }
}

//
//
// FIXME, put this stuff in a common place
//
//

// FIXME, dataWindowForLevel
Box2i
TiledInputFile::pixelRangeForLevel (int l) const
{
    return pixelRangeForLevel(l, l);
}


Box2i
TiledInputFile::pixelRangeForLevel (int lx, int ly) const
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

        return Box2i();
    }
}


// FIXME, dataWindowForTile
Box2i
TiledInputFile::pixelRangeForTile (int dx, int dy, int l) const
{
    return pixelRangeForTile(dx, dy, l, l);
}


Box2i
TiledInputFile::pixelRangeForTile (int dx, int dy, int lx, int ly) const
{
    try
    {
        if (!isValidTile(dx,dy,lx,ly))
            throw Iex::ArgExc ("Parameters not in valid range.");

        V2i tileMin = V2i(_data->minX + dx * tileXSize(),
                          _data->minY + dy * tileYSize());

        V2i tileMax = tileMin + V2i(tileXSize() - 1, tileYSize() - 1);

	// FIXME, use dataWindowForTile
        V2i levelMax = V2i(_data->minX, _data->minY) +
                       V2i(levelWidth(lx) - 1, levelHeight(ly) -1);

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
                        "file \"" << fileName() << "\". " << dx <<
                        ", " << dy << ", " << lx << ", " << ly << e);
        throw;

        return Box2i();
    }
}


bool
TiledInputFile::isValidTile(int dx, int dy, int lx, int ly) const
{
    return ((lx < numXLevels() && lx >= 0) &&
             (ly < numYLevels() && ly >= 0) &&
             (dx < numXTiles(lx) && dx >= 0) &&
             (dy < numYTiles(ly) && dy >= 0));
}


//
// FIXME, put this in ImfInputFile and make a temporary buffer that is the width of the whole level.
// Reuse if the next readPixels overlaps our cached temporary buffer.

// In the ImfInputFile version:
//
// In setFrameBuffer, invalidate the cached buffer if the new frameBuffer has different types than the old.
//

//
// Reads all level (0,0) tiles overlap the scanline interval provided,
// and store them in the user's frame buffer.
//
// This function facilitates the use of the old scanline-based API on
// tiled files.
//
void
TiledInputFile::readPixels(int scanLine1, int scanLine2)
{
    try
    {
#ifdef PLATFORM_WIN32
        int minY = min (scanLine1, scanLine2);
        int maxY = max (scanLine1, scanLine2);
#else
        int minY = std::min (scanLine1, scanLine2);
        int maxY = std::max (scanLine1, scanLine2);
#endif

        if (minY < _data->minY || maxY >  _data->maxY)
        {
            throw Iex::ArgExc ("Tried to read scan line outside "
                                "the image file's data window.");
        }

        //
        // The minimum and maximum y tile coordinates that intersect this
        // scanline range
        //

        int minDy = (minY - _data->minY)/tileYSize();
        int maxDy = (maxY - _data->minY)/tileYSize();

        //
        // Figure out which one is first in the file so we can read without seeking
        //

        int yStart, yEnd, yStep;
        if (_data->lineOrder == DECREASING_Y)
        {
            yStart = maxDy;
            yEnd = minDy-1;
            yStep = -1;
        }
        else
        {
            yStart = minDy;
            yEnd = maxDy+1;
            yStep = 1;
        }

        // backup the user's framebuffer
        FrameBuffer oldBuffer = frameBuffer();

        // the number of pixels in 1 tile
        int tileSize = tileXSize()*tileYSize();

        //
        // Read the tiles into our temporary framebuffer and copy them into
        // the user's buffer
        //

        for (int j = yStart; j != yEnd; j += yStep)
        {
            for (int i = 0; i < numXTiles(0); ++i)
            {
                Box2i tileRange = pixelRangeForTile(i, j, 0);

#ifdef PLATFORM_WIN32
                int minYThisTile = max (minY, tileRange.min.y);
                int maxYThisTile = min (maxY, tileRange.max.y);
#else
                int minYThisTile = std::max (minY, tileRange.min.y);
                int maxYThisTile = std::min (maxY, tileRange.max.y);
#endif

                //
                // Create a temporary framebuffer to hold one tile and create a new
                // slice for each slice in oldBuffer. Our new slices will only have
                // to hold one tile of data at a time
                //

                FrameBuffer tempBuffer;

                int offset = (tileRange.min.y - _data->minY)*tileXSize() +
                             (tileRange.min.x - _data->minX);

                for (FrameBuffer::ConstIterator k = oldBuffer.begin();
                     k != oldBuffer.end(); ++k)
                {
                    Slice s = k.slice();
                    switch (s.type)
                    {
                        case UINT:

                            tempBuffer.insert(k.name(),
                            Slice(UINT,
                            (char *)((new unsigned int[tileSize]) -
                                     offset),
                            sizeof(unsigned int),
                            sizeof(unsigned int) * tileXSize()));

                        break;

                        case HALF:

                            tempBuffer.insert(k.name(),
                            Slice(HALF,
                            (char *)((new half[tileSize]) -
                                     offset),
                            sizeof(half),
                            sizeof(half) * tileXSize()));

                        break;

                        case FLOAT:
                        
                            tempBuffer.insert(k.name(),
                            Slice(FLOAT,
                            (char *)((new float[tileSize]) -
                                     offset),
                            sizeof(float),
                            sizeof(float) * tileXSize()));

                        break;
                        
                        default:
                        
                            throw Iex::ArgExc ("Unknown pixel data type.");
                    }
                }

                setFrameBuffer(tempBuffer);
                
                readTile(i, j, 0);
                
                //
                // copy the data from our temporary tile framebuffer into the
                // user's framebuffer
                //
                
                for (FrameBuffer::ConstIterator k = tempBuffer.begin();
                      k != tempBuffer.end();
                      ++k)
                {
                    Slice fromSlice = k.slice();        // slice to write from
                    Slice toSlice = oldBuffer[k.name()];// slice to write to

                    char *fromPtr, *toPtr;
                    int size = pixelTypeSize(toSlice.type);

                    for (int y = minYThisTile; y <= maxYThisTile; ++y)
                    {
                        // set the pointers to the start of the y scanline in
                        // this tile
                        fromPtr = fromSlice.base +
                                  (y - _data->minY) * fromSlice.yStride +
                                  (tileRange.min.x - _data->minX) *
                                  fromSlice.xStride;

                        toPtr   = toSlice.base +
                                  (y - _data->minY) * toSlice.yStride +
                                  (tileRange.min.x - _data->minX) *
                                  toSlice.xStride;

                        // copy all pixels in this tile's scanline
                        for (int x = tileRange.min.x;
                             x <= tileRange.max.x;
                             ++x)
                        {
                            for (size_t i = 0; i < size; ++i)
                                toPtr[i] = fromPtr[i];

                            fromPtr += fromSlice.xStride;
                            toPtr += toSlice.xStride;
                        }
                    }
                }
                
                // delete all the slices in the temporary frameBuffer
                for (FrameBuffer::ConstIterator k = tempBuffer.begin();
                     k != tempBuffer.end(); ++k)
                {
                    Slice s = k.slice();
                    switch (s.type)
                    {
                        case UINT:
                            delete [] (((unsigned int*)s.base) + offset);
                        break;

                        case HALF:
                        {
                            delete [] ((half*)s.base + offset);
                        }
                        break;

                        case FLOAT:
                            delete [] (((float*)s.base) + offset);
                        break;
                    }                
                }
            }
        }

        setFrameBuffer(oldBuffer);
    }
    catch (Iex::BaseExc &e)
    {
        REPLACE_EXC (e, "Error reading pixel data from image "
                        "file \"" << fileName() << "\". " << e);
        throw;
    }
}


} // namespace Imf
