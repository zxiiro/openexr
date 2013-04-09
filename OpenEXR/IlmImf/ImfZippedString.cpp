///////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2013, Weta Digital Ltd
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
// *       Neither the names of Weta Digital Ltd, Industrial Light & Magic
// nor the names of their contributors may be used to endorse or promote products derived
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



#include "ImfZippedString.h"
#include "ImfIO.h"
#include "ImfXdr.h"
#include <vector>
#include <zlib.h>
#include <string.h>
#include <math.h>

OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_ENTER

using std::string;
using std::vector;
class ZippedString::Data{
public:
    bool _isCompressed;            ///< true if the string is compressed (stored in _data), rather than uncompressed (stored in _str)
    size_t _unCompressedSize;      ///< size of string if uncompressed
    string _str;                   ///< holds string when uncompressed
    vector<Bytef> _data;           ///< holds compressed data when compressed
    
    string  unzipped() const;      ///< return a copy of the string uncompressed
    void zip();                    ///< set string to be compressed: compress string, store in _data, release _str
    void unzip();
    Data();
};

ZippedString::Data::Data() : _isCompressed(false)
{
  
}

string
ZippedString::Data::unzipped() const
{
    vector<Bytef> uncomp(_unCompressedSize);
    uLongf outSize = _unCompressedSize;
      if (Z_OK != ::uncompress (&uncomp[0], &outSize,
                                    &_data[0], _data.size() ))
    {
          throw IEX_NAMESPACE::InputExc ("String decompression (zlib) failed.");
    }
    return string(uncomp.begin(),uncomp.end());

}

void
ZippedString::Data::zip()
{
        _unCompressedSize=_str.size();

   uLongf outSize = int(ceil(_unCompressedSize * 1.01)) + 100;
   vector<Bytef> _tmpBuffer(outSize);
   

    if (Z_OK != ::compress (&_tmpBuffer[0], &outSize,
                                  (const Bytef *) _str.c_str(), _unCompressedSize))
    {
          throw IEX_NAMESPACE::BaseExc ("String compression (zlib) failed.");
    }

    _data.resize(outSize);
    memcpy((void *) &_data[0],(void *) &_tmpBuffer[0],size_t(outSize));
    _isCompressed=true;
    _str.clear();
}

void ZippedString::Data::unzip()
{
  _str=unzipped();
  _data=vector<Bytef>();
  _isCompressed=false;
}


bool ZippedString::isCompressed() const
{
   return _data->_isCompressed;
}

ZippedString::ZippedString() : _data(new Data) {}

ZippedString::ZippedString ( const string& str ) : _data(new Data)
{
   _data->_str=str;
}

ZippedString::ZippedString ( const char* str ) : _data(new Data)
{
  _data->_str=string(str);
}

ZippedString::ZippedString ( const ZippedString& str ) : _data(new Data)
{
   _data->_isCompressed=str._data->_isCompressed;
   if(_data->_isCompressed)
   {
       _data->_unCompressedSize=str._data->_unCompressedSize;
       _data->_data=str._data->_data;
   }else{
       _data->_str =str._data->_str;
   }
}

ZippedString& ZippedString::operator= ( const ZippedString& other )
{
    _data->_isCompressed=other._data->_isCompressed;
   if(_data->_isCompressed)
   {
       _data->_unCompressedSize=other._data->_unCompressedSize;
       _data->_data=other._data->_data;
       _data->_str.clear();
   }else{
       _data->_str =other._data->_str;
       _data->_data=vector<Bytef>();
 
       
   }
   return *this;
}

bool ZippedString::operator== ( const ZippedString & other ) const
{
   if(other._data->_isCompressed)
   {
       if(_data->_isCompressed)
       {
           return _data->_data==other._data->_data;
       }else{
           
           // other is zipped, but this isn't
           if(other._data->_unCompressedSize!=_data->_str.size())
           {
               return false;
           }
           Data tmp;
           tmp._isCompressed=true;
           tmp._str=_data->_str;
           tmp.zip();
           return other._data->_data==tmp._data;
       }
   }else{
       if(_data->_isCompressed)
       {
           //we are zipped, but other isn't
           if(_data->_unCompressedSize!=other._data->_str.size())
           {
               return false;
           }
           Data tmp;
           tmp._isCompressed=true;
           tmp._str=other._data->_str;
           tmp.zip();
           return tmp._data==_data->_data;
       }
   }
}

string& 
ZippedString::str()
{
   if(_data->_isCompressed)
   {
       _data->unzip();
   }
   return _data->_str;
}

string
ZippedString::str() const
{
   if(_data->_isCompressed)
   {
       return _data->unzipped();
   }else{
       return _data->_str;
   }
}

ZippedString::~ZippedString()
{
   delete _data;
}

void
ZippedString::writeString ( OStream& s  ) const
{
   if(isCompressed())
   {
       Xdr::write<StreamIO>(s,int(_data->_unCompressedSize));
       for(size_t i=0;i<_data->_data.size();i++)
       {
           Xdr::write<StreamIO>(s,_data->_data[i]);
       }
   }else{
       Data tmp;
       tmp._isCompressed=false;
       tmp._str=_data->_str;
       tmp.zip();
       Xdr::write<StreamIO>(s,int(tmp._unCompressedSize));
       for(size_t i=0;i<tmp._data.size();i++)
       {
           Xdr::write<StreamIO>(s,tmp._data[i]);
       }
   }
}

void
ZippedString::readString ( IStream& s ,int size)
{
   _data->_isCompressed=true;
   int uncompressed_size;
   Xdr::read<StreamIO>(s,uncompressed_size);
   _data->_unCompressedSize=uncompressed_size;
   _data->_data.resize(size-Xdr::size<int>());
   for(size_t i=0;i<_data->_data.size();i++)
   {
       Xdr::read<StreamIO>(s,_data->_data[i]);
   }
   _data->_str.clear();
}


OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_EXIT