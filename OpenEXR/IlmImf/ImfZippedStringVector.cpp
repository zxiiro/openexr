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



#include "ImfZippedStringVector.h"
#include "ImfIO.h"
#include "ImfXdr.h"
#include <vector>
#include <zlib.h>
#include <string.h>
#include <math.h>

OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_ENTER

using std::string;
using std::vector;
class ZippedStringVector::Data{
public:
    bool _isCompressed;            ///< true if the string vector is compressed (stored in _data), rather than uncompressed (stored in _str)
    size_t _unCompressedSize;      ///< count of storage required to uncompress string (including size tables)
    vector<string> _vec;           ///< holds string vector when uncompressed
    vector<Bytef> _data;           ///< holds compressed data when compressed
    void unzipped(vector<string> & out) const;      ///< set out to be a copy of the string vector uncompressed
    void zip();                    ///< set string to be compressed: compress string, store in _data, release _str
    void unzip();
    Data();
};

ZippedStringVector::Data::Data() : _isCompressed(false)
{
  
}

void
ZippedStringVector::Data::unzipped( std::vector< std::string >& out ) const
{
    vector<Bytef> uncomp(_unCompressedSize);
    uLongf outSize = _unCompressedSize;
      if (Z_OK != ::uncompress (&uncomp[0], &outSize,
                                    &_data[0], _data.size() ))
    {
          throw IEX_NAMESPACE::InputExc ("String decompression (zlib) failed.");
    }
    
    const char* in=(char*) &uncomp[0];
    int size;
    Xdr::read<CharPtrIO>(in,size);
    out.resize(size);
    for( size_t i=0 ; i<out.size() ; i++ )
    {
        Xdr::read<CharPtrIO>(in,size);
        out[i]=string(in,size);
        in+=size;
        
    }

}

void
ZippedStringVector::Data::zip()
{
   
    _unCompressedSize=(_vec.size()+1)*Xdr::size<int>();
    for(size_t i=0;i<_vec.size();i++)
    {
        _unCompressedSize+=_vec[i].size();
    }

   vector<Bytef> _tmpInBuffer(_unCompressedSize);
   char* ptr=(char*) &_tmpInBuffer[0];
   
   Xdr::write<CharPtrIO>(ptr,int(_vec.size()));
   for(size_t i=0;i<_vec.size();i++)
   {
      Xdr::write<CharPtrIO>(ptr,int(_vec[i].size()));
      memcpy(ptr,_vec[i].c_str(),_vec[i].size());
      ptr+=_vec[i].size();
   }
   
    
   uLongf outSize = int(ceil(_unCompressedSize * 1.01)) + 100;
   vector<Bytef> _tmpOutBuffer(outSize);
   
   
   

    if (Z_OK != ::compress (&_tmpOutBuffer[0], &outSize,
                             &_tmpInBuffer[0], _unCompressedSize))
    {
          throw IEX_NAMESPACE::BaseExc ("String compression (zlib) failed.");
    }

    _data.resize(outSize);
    memcpy((void *) &_data[0],(void *) &_tmpOutBuffer[0],size_t(outSize));
    _isCompressed=true;
    _vec=vector<string>();
   
}

void ZippedStringVector::Data::unzip()
{
  unzipped(_vec);
  _data=vector<Bytef>();
  _isCompressed=false;
}


bool ZippedStringVector::isCompressed() const
{
   return _data->_isCompressed;
}

ZippedStringVector::ZippedStringVector() : _data(new Data) {}

ZippedStringVector::ZippedStringVector ( const vector<string> & str ) : _data(new Data)
{
   _data->_vec=str;
}


ZippedStringVector::ZippedStringVector ( const ZippedStringVector& str ) : _data(new Data)
{
   _data->_isCompressed=str._data->_isCompressed;
   if(_data->_isCompressed)
   {
       _data->_unCompressedSize=str._data->_unCompressedSize;
       _data->_data=str._data->_data;
   }else{
       _data->_vec =str._data->_vec;
   }
}

ZippedStringVector& ZippedStringVector::operator= ( const ZippedStringVector& other )
{
    _data->_isCompressed=other._data->_isCompressed;
   if(_data->_isCompressed)
   {
       _data->_unCompressedSize=other._data->_unCompressedSize;
       _data->_data=other._data->_data;
       _data->_vec.clear();
   }else{
       _data->_vec =other._data->_vec;
       _data->_data=vector<Bytef>();
 
       
   }
   return *this;
}

bool ZippedStringVector::operator== ( const ZippedStringVector & other ) const
{
   if(other._data->_isCompressed)
   {
       if(_data->_isCompressed)
       {
           return _data->_data==other._data->_data;
       }else{
           
           // other is zipped, but this isn't
           if(other._data->_unCompressedSize!=_data->_vec.size())
           {
               return false;
           }
           Data tmp;
           tmp._isCompressed=true;
           tmp._vec=_data->_vec;
           tmp.zip();
           return other._data->_data==tmp._data;
       }
   }else{
       if(_data->_isCompressed)
       {
           //we are zipped, but other isn't
           if(_data->_unCompressedSize!=other._data->_vec.size())
           {
               return false;
           }
           Data tmp;
           tmp._isCompressed=true;
           tmp._vec=other._data->_vec;
           tmp.zip();
           return tmp._data==_data->_data;
       }else{
           // both unzipped
           return other._data->_vec==_data->_vec;
       }
   }
}
vector< string >& 
ZippedStringVector::vec()
{
   if(_data->_isCompressed)
   {
       _data->unzip();
   }
   return _data->_vec;
}

vector<string> 
ZippedStringVector::vec() const
{
   if(_data->_isCompressed)
   {
       vector<string> tmp;
       _data->unzipped(tmp);
       return tmp;
   }else{
       return _data->_vec;
   }
}

ZippedStringVector::~ZippedStringVector()
{
   delete _data;
}

void
ZippedStringVector::writeStringVector ( OStream& s  ) const
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
       tmp._vec=_data->_vec;
       tmp.zip();
       Xdr::write<StreamIO>(s,int(tmp._unCompressedSize));
       for(size_t i=0;i<tmp._data.size();i++)
       {
           Xdr::write<StreamIO>(s,tmp._data[i]);
       }
   }
}

void
ZippedStringVector::readStringVector ( IStream& s ,int size)
{
   _data->_isCompressed=true;
   if(size<Xdr::size<int>())
   {
       throw(IEX_NAMESPACE::IoExc("corrupt zipped string vector: too small"));
   }
   int uncompressed_size;
   Xdr::read<StreamIO>(s,uncompressed_size);
   _data->_unCompressedSize=uncompressed_size;
   _data->_data.resize(size-Xdr::size<int>());
   for(size_t i=0;i<_data->_data.size();i++)
   {
       Xdr::read<StreamIO>(s,_data->_data[i]);
   }
   _data->_vec.clear();
}


OPENEXR_IMF_INTERNAL_NAMESPACE_SOURCE_EXIT
