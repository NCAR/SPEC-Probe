
#ifndef __SPEC_RLE__
#define __SPEC_RLE__

#include <cassert>
#include <cstring>
#include <iostream>


/**
 * Run-length encoding un-encode routine.  Decompress a 4k buffer from a SPEC Inc
 * probe.
 *
 */
class SpecDecompress {

public:


  SpecDecompress(int timeWordLen, bool dbg = false) : _particle(0), _prevParticleID(0), _prevTimeWord(0), _timingWordLength(timeWordLen), _multiPacketParticle(false), _debug(dbg)
  {

  }

  ~SpecDecompress()
  {
    delete [] _particle;
  }


  /// Decompress a record.
size_t decompressSPEC(const uint16_t wp[], unsigned char *output)
{
  size_t nSlices = 0;
  int j = 0, nImgWords = 0;

  if (_debug)
    std::cout << "decompressSPEC\n";

  if (_particle == 0)
    _particle = new uint16_t[4096];

  /* Loop over image buffer, decompressing RLE, copy out one particle at a time
   * and call decompressParticle().
   */
  for (j = 0; j < 2040; ++j)
  {
    // I believe these are filtered in the datastore...if first word in
    // buffer (i.e. j==0).
    if (wp[j] == FlushWord)              // NL flush buffer
    {
      if (_debug)
        std::cout << " NL flush\n";
      break;
    }


    // Start of particle
    if ( isParticleSyncWord(&wp[j]) )
    {
      if (_debug)
        std::cout << " start of particle, j=" << j << " NH/NV=" << wp[j+1] << ", "
		<< wp[j+2] << std::endl;

      memcpy(_particle, &wp[j], 5 * sizeof(uint16_t));
      nImgWords = extractNimageWords(_particle);
      j += 5;

      /* This is part of particles with no timing word, they extend across many(?)
       * buffers.  I don't know how to handle them at this time, or whether we
       * need to.  Generally a stuck bit.
       */
      bool reject = false;

      if (nImgWords == 0 || nImgWords > 950)	// seems runaway @ 960ish
      {
        reject = true;
      }

      // I am rejecting these at this time.  Typically runaway
      // @TODO improve this for legit multi-packet.
      if (_particle[ID] == _prevParticleID || _multiPacketParticle)
      {
        reject = true;
      }

      _prevParticleID = _particle[ID];

      if (reject)
      {
        if (_debug)
          std::cout << "  rejecting particle.  ID="
		<< _particle[ID] << ", nWords=" << nImgWords << ", multi-packet=" << _multiPacketParticle << "\n";
        j += nImgWords -1;
        continue;
      }

      if (j + nImgWords > 2048)
      {
        // Don't handle particles that carryover into next record...for now.
        if (_debug)
          std::cout << " short image, j=" << j << ", n=" << nImgWords << std::endl;
        break;
      }

      memcpy(&_particle[5], &wp[j], nImgWords * sizeof(uint16_t));
      j += (nImgWords-1);


      // OK, we can process particle
if (nSlices > 1800) std::cerr << "nSlices getting large = " << nSlices;
      nSlices += decompressParticle((uint16_t *)_particle, &output[nSlices*16]);
    }
  }

  if (_debug)
    std::cout << " returning nSlices=" << nSlices << std::endl;
  return nSlices;
}


bool isParticleSyncWord(const uint16_t *p) const
{
  if (p[0] == SyncWord && ((p[1] == 0) != (p[2] == 0)))  // Should this be an xclusive or?
    return true;

  return false;
}


//private:

size_t decompressParticle(const uint16_t *input, unsigned char *output)
{
  int i, nSlices = input[N_SLICES], nWords, sliceCnt = 0, bitPos = 0;
  uint16_t value;
  bool timingWord = true;
  bool sliceStarted = false;

  if (_debug)
    std::cout << "  decompressParticle 0x" << std::hex << input[0] << " - ID=" << std::dec << input[3] << ", nSlices=" << nSlices << std::endl;

  if (input[2] != 0)
  {
    nWords = input[2] & 0x0FFF;
    timingWord = !(input[2] & 0x1000);
//    DLOG << "   H nWords=" << nWords << " - " << (timingWord ? "" : "NT");
  }
  else
  {
    nWords = input[1] & 0x0FFF;
    timingWord = !(input[1] & 0x1000);
 //   DLOG << "   V nWords=" << nWords << " - " << (timingWord ? "" : "NT");
  }

  input += 5;
  if (timingWord) nWords -= _timingWordLength;

  if (nSlices > nWords)
  {
    std::cerr << "   Error: nSlices greater than nWords, bailing.\n";
    return 0;
  }

  // Preset output to all one's.
  memset(output, 0x00, nSlices*16);

  for (i = 0; i < nWords; ++i)
  {
    if (input[i] == 0x4000)	// Full shadowed slice.
    {
// has a sliceCnt issue if not first slice of record
      if (_debug)
        std::cout << "             ------------ Fully Shadowed Slice ------------\n";
      if (sliceStarted == true) ++sliceCnt;
      memset(&output[sliceCnt*16], 0xff, 16);
      sliceStarted = false;
      bitPos = 0;
      ++sliceCnt;
      continue;
    }

    if (input[i] == 0x7FFF)	// Uncompressed slice.
    {
// has a sliceCnt issue if not first slice of record
      if (_debug)
        std::cout << "             ------------ Uncompressed Slice ------------\n";
      if (sliceStarted == true) ++sliceCnt;
      memcpy(&output[sliceCnt*16], &input[i+1], 16);
      i += 8;
      sliceStarted = false;
      bitPos = 0;
      ++sliceCnt;
      continue;
    }

    if (input[i] & 0x4000)	// First word of slice
    {
      if (sliceStarted == true) ++sliceCnt;
      sliceStarted = true;
      memset(&output[sliceCnt*16], 0, 16);	// zero out next slice.
      bitPos = 0;
    }

    if ((value = (input[i] & 0x007F)) > 0)	// Number clear pixels
    {
      bitPos += value;
    }

    if ((value = (input[i] & 0x3F80) >> 7) > 0)	// Number shaded pixels
    {
      int offset = sliceCnt*16;
      for (int j = 0; j < value;  ++j, ++bitPos)
      {
        output[(offset)+bitPos/8] |= (0x80 >> bitPos%8);
      }

//      bitPos += value;	// Claude says redundant
    }
  }

  if (sliceStarted)	// close out an open slice
      ++sliceCnt;

  if (sliceCnt != nSlices)
  {
    std::cerr << "  sliceCnt != nSlices, abandoning particle.\n";
    std::cerr << "   end - " << i << "/"<<nWords<<" words, sliceCnt="<<sliceCnt<<"/"<<nSlices<<std::endl;
    return 0;
  }

  if (timingWord)
  {
    unsigned long tWord = ((unsigned long *)&input[nWords])[0] & 0x0000FFFFFFFFFFFF;

    memcpy(&output[sliceCnt*16], &input[nWords], 6);
    memset(&output[sliceCnt*16+6], 0x00, 2);
    memset(&output[sliceCnt*16+8], 0xaa, 8);
    ++sliceCnt;

    if (_debug)
      std::cout << "  tWord=" << tWord << std::endl; // << ", deltaT=" << tWord - _prevTimeWord;
    _prevTimeWord = tWord;
  }
  else
    if (_debug)
      std::cout << "  NO TIMING WORD\n";

  if (_debug)
    std::cout << "   end - " << i << "/"<<nWords<<" words, sliceCnt="<<sliceCnt<<"/"<<nSlices<<std::endl;
  return((size_t)sliceCnt);
}



int extractNimageWords(uint16_t *p)
{
  uint16_t nh = p[1] & 0x0FFF;
  uint16_t nv = p[2] & 0x0FFF;

  if (_debug)
    std::cout << "  pHdr ID="<<p[0]<<", ID="<<p[3]<<", nh="<<nh<<", nv="<<nv<<", nSlice="<<p[N_SLICES]<<std::endl;

  if ((nh == 0) == (nv == 0))	// This should never occur - one and only one should be zero
    return 0;

  if (nh > 0)
    _multiPacketParticle = p[1] & 0x1000;
  else
    _multiPacketParticle = p[2] & 0x1000;

  return std::max(nh, nv);
}


  uint16_t *_particle;
  uint16_t _prevParticleID;
  unsigned long _prevTimeWord;
  int _timingWordLength;

  bool _multiPacketParticle;
  bool _debug;

  static const uint16_t SyncWord = 0x3253;       // Particle sync word.
  static const uint16_t FlushWord = 0x4e4c;      // NL Flush Buffer.

  static const int ID = 3;		// Index for Particle ID
  static const int N_SLICES = 4;	// Index for number of uncompressed slices

private:

    /** No copying. */
    SpecDecompress(const SpecDecompress&);

    /** No copying. */
    SpecDecompress& operator=(const SpecDecompress&);

};

#endif
