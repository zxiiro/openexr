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
//	class TileOffsets
//
//-----------------------------------------------------------------------------

#include <ImfTileOffsets.h>
#include <ImfXdr.h>
#include <ImfIO.h>
#include <Iex.h>

namespace Imf {


TileOffsets::TileOffsets (LevelMode mode,
			  int numXLevels, int numYLevels,
			  int* numXTiles, int* numYTiles) :
	_mode(mode), _numXLevels(numXLevels), _numYLevels(numYLevels)
{
    switch (_mode)
    {
      case ONE_LEVEL:
      case MIPMAP_LEVELS:

        _tileOffsets.resize(_numXLevels);

        // iterator over all levels
        for (size_t i_l = 0; i_l < _tileOffsets.size(); ++i_l)
        {
            _tileOffsets[i_l].resize(numYTiles[i_l]);

            // in this level, iterate over all y Tiles
            for (size_t i_dy = 0;
                 i_dy < _tileOffsets[i_l].size();
                 ++i_dy)
            {
                _tileOffsets[i_l][i_dy].resize(numXTiles[i_l]);
            }
        }
        break;

      case RIPMAP_LEVELS:

        _tileOffsets.resize(_numXLevels * _numYLevels);

        // iterator over all ly's
        for (size_t i_ly = 0; i_ly < _numYLevels; ++i_ly)
        {
            // iterator over all lx's
            for (size_t i_lx = 0; i_lx < _numXLevels; ++i_lx)
            {
                int index = i_ly * _numXLevels + i_lx;
                _tileOffsets[index].resize(numYTiles[i_ly]);

                // and iterator over all y Tiles in level (i_lx, i_ly)
                for (size_t i_dy = 0;
                     i_dy < _tileOffsets[index].size();
                     ++i_dy)
                {
                    _tileOffsets[index][i_dy].resize(numXTiles[i_lx]);
                }
            }
        }
        break;
    }
}


bool
TileOffsets::checkForErrors()
{
    bool error = false;
    
    for (size_t i_l = 0; i_l < _tileOffsets.size() && !error; ++i_l)
    {
	// in this level, iterate over all y Tiles
	for (size_t i_dy = 0;
	    i_dy < _tileOffsets[i_l].size() && !error; ++i_dy)
	{
	    // and iterate over all x Tiles
	    for (size_t i_dx = 0;
		 i_dx < _tileOffsets[i_l][i_dy].size() && !error; ++i_dx)
	    {
		if (_tileOffsets[i_l][i_dy][i_dx] <= 0)
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
    
    return error;
}


bool
TileOffsets::readTile (ifstream& is)
{
    if (!is)
	return true;

    long tileOffset = is.tellg();

    int tileX;
    Xdr::read <StreamIO> (is, tileX);

    if (!is)
	return true;

    int tileY;
    Xdr::read <StreamIO> (is, tileY);

    if (!is)
	return true;

    int levelX;
    Xdr::read <StreamIO> (is, levelX);

    if (!is)
	return true;

    int levelY;
    Xdr::read <StreamIO> (is, levelY);

    if (!is)
	return true;

    int dataSize;
    Xdr::read <StreamIO> (is, dataSize);

    if (!is)
	return true;

    Xdr::skip <StreamIO> (is, dataSize);

    if (!isValidTile(tileX, tileY, levelX, levelY))
        return true;

    operator () (tileX, tileY, levelX, levelY) = tileOffset;
    
    return false;
}


void
TileOffsets::reconstructFromFile (ifstream& is)
{
    //
    // The tile index stores the offset in the file for each tile. This is
    // usually stored towards the beginning of the file. If the tile index is
    // not complete (the file writing was aborted) this function will seek
    // through the whole file, and reconstructs the tile index if possible.
    //

    long position = is.tellg();

    try
    {
	//
	// Seek to each tile and record its offset
	//

	// iterator over all levels
	for (size_t i_l = 0; i_l < _tileOffsets.size(); ++i_l)
	{
	    // in this level, iterate over all y Tiles
	    for (size_t i_dy = 0;
	         i_dy < _tileOffsets[i_l].size(); ++i_dy)
	    {
		// and iterate over all x Tiles
		for (size_t i_dx = 0;
		    i_dx < _tileOffsets[i_l][i_dy].size(); ++i_dx)
		{
		    if (readTile(is))
			throw Iex::ArgExc ("Error when reconstructing tile offsets.");
		}
	    }
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

        //
        // When trying to reconstruct the tile offsets from the file We break
        // out on the first exception. We do not try to recover tiles after the
        // first exception.
        //
    }

    is.clear();
    is.seekg (position);
}


void
TileOffsets::readFrom (ifstream& is)
{
    //
    // Read in the tile offsets from the file's index
    //
    // iterator over all levels
    for (size_t i_l = 0; i_l < _tileOffsets.size(); ++i_l)
    {
	// in this level, iterate over all y Tiles
	for (size_t i_dy = 0;
	     i_dy < _tileOffsets[i_l].size(); ++i_dy)
	{
	    // and iterate over all x Tiles
	    for (size_t i_dx = 0;
		 i_dx < _tileOffsets[i_l][i_dy].size(); ++i_dx)
	    {
		Xdr::read <StreamIO> (is,
			_tileOffsets[i_l][i_dy][i_dx]);
	    }
	}
    }

    //
    // Error check the tile offsets
    //

    if (checkForErrors())
	reconstructFromFile (is);
}


long
TileOffsets::writeTo (ofstream& os)
{
    //
    // Writes the tile index to the file. Returns the start position of the
    // index in the file.
    //
    
    long pos = os.tellp();

    if (pos == -1)
	Iex::throwErrnoExc ("Cannot determine current file position (%T).");

             
    // iterate over all levels
    for (size_t i_l = 0; i_l < _tileOffsets.size(); ++i_l)
    {
	// in this level, iterate over all y Tiles
	for (size_t i_dy = 0;
	    i_dy < _tileOffsets[i_l].size(); ++i_dy)
	{
	    // and iterate over all x Tiles
	    for (size_t i_dx = 0;
		 i_dx < _tileOffsets[i_l][i_dy].size(); ++i_dx)
	    {
		Xdr::write <StreamIO> (os,
				       _tileOffsets[i_l][i_dy][i_dx]);
	    }
	}
    }

    return pos;
}


bool
TileOffsets::isEmpty ()
{
    // iterate over all levels
    for (size_t i_l = 0; i_l < _tileOffsets.size(); ++i_l)
    {
	// in this level, iterate over all y Tiles
	for (size_t i_dy = 0;
	     i_dy < _tileOffsets[i_l].size(); ++i_dy)
	{
	    // and iterate over all x Tiles
	    for (size_t i_dx = 0;
		 i_dx < _tileOffsets[i_l][i_dy].size(); ++i_dx)
	    {
		if (_tileOffsets[i_l][i_dy][i_dx] != 0)
		    return false;
	    }
	}
    }
    
    return true;
}

bool
TileOffsets::isValidTile(int dx, int dy, int lx, int ly)
{
    switch (_mode)
    {
      case ONE_LEVEL:

        if (lx == 0 && ly == 0 && _tileOffsets.size() > 0 &&
            _tileOffsets[0].size() > dy &&
            _tileOffsets[0][dy].size() > dx)
            return true;

        break;

      case MIPMAP_LEVELS:

        if (lx < _numXLevels && ly < _numYLevels &&
            _tileOffsets.size() > lx &&
            _tileOffsets[lx].size() > dy &&
            _tileOffsets[lx][dy].size() > dx)
            return true;

        break;

      case RIPMAP_LEVELS:

        if (lx < _numXLevels && ly < _numYLevels &&
            _tileOffsets.size() > lx + ly*_numXLevels &&
            _tileOffsets[lx + ly*_numXLevels].size() > dy &&
            _tileOffsets[lx + ly*_numXLevels][dy].size() > dx)
            return true;

        break;

      default:

        return false;
    }
    
    return false;
}


long&
TileOffsets::operator () (int dx, int dy, int lx, int ly)
{
    //
    // Looks up the value of the tile with tile coordinate (dx, dy) and
    // level number (lx, ly) in the tileOffsets array and returns the
    // cooresponding offset.
    //

    switch (_mode)
    {
      case ONE_LEVEL:

        return _tileOffsets[0][dy][dx];
        break;

      case MIPMAP_LEVELS:

        return _tileOffsets[lx][dy][dx];
        break;

      case RIPMAP_LEVELS:

        return _tileOffsets[lx + ly*_numXLevels][dy][dx];
        break;

      default:

        throw Iex::ArgExc ("Unknown LevelMode format.");
    }
}


long&
TileOffsets::operator () (int dx, int dy, int l)
{
    return operator () (dx, dy, l, l);
}


const long&
TileOffsets::operator () (int dx, int dy, int lx, int ly) const
{
    //
    // Looks up the value of the tile with tile coordinate (dx, dy) and
    // level number (lx, ly) in the tileOffsets array and returns the
    // cooresponding offset.
    //

    switch (_mode)
    {
      case ONE_LEVEL:

        return _tileOffsets[0][dy][dx];
        break;

      case MIPMAP_LEVELS:

        return _tileOffsets[lx][dy][dx];
        break;

      case RIPMAP_LEVELS:

        return _tileOffsets[lx + ly*_numXLevels][dy][dx];
        break;

      default:

        throw Iex::ArgExc ("Unknown LevelMode format.");
    }
}


const long&
TileOffsets::operator () (int dx, int dy, int l) const
{
    return operator () (dx, dy, l, l);
}


} // namespace Imf
