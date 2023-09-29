// Various defs for various versions of glibc to make endian.h working
#define _DEFAULT_SOURCE 1
#define __USE_BSD
#define __USE_MISC
#include <endian.h>

#define LOG_PREFIX "frame"
#include <tetrapol/log.h>
#include <tetrapol/bit_utils.h>
#include <tetrapol/tetrapol.h>
#include <tetrapol/frame.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// used when decoding firts part of frame, common to data and voice frames
enum {
    FRAME_DATA_LEN1 = 52,
};

struct frame_decoder_priv_t {
    int band;
    int scr;
    int fr_type;
};

struct frame_encoder_priv_t {
    int band;
    int scr;    ///< scramblink constant
    int dir;    ///< channel direction - downlink or uplink
    uint8_t first_bit;  ///< carry bit for differential frame encoding
};

/**
  PAS 0001-2 6.1.5.1
  PAS 0001-2 6.2.5.1
  PAS 0001-2 6.3.4.1

  Scrambling sequence was generated by this python3 script

  s = [1, 1, 1, 1, 1, 1, 1]
  for k in range(len(s), 127):
    s.append(s[k-1] ^ s[k-7])
  for i in range(len(s)):
    print(s[i], end=", ")
    if i % 8 == 7:
      print()
  */
static uint8_t scramb_table[127] = {
    1, 1, 1, 1, 1, 1, 1, 0,
    1, 0, 1, 0, 1, 0, 0, 1,
    1, 0, 0, 1, 1, 1, 0, 1,
    1, 1, 0, 1, 0, 0, 1, 0,
    1, 1, 0, 0, 0, 1, 1, 0,
    1, 1, 1, 1, 0, 1, 1, 0,
    1, 0, 1, 1, 0, 1, 1, 0,
    0, 1, 0, 0, 1, 0, 0, 0,
    1, 1, 1, 0, 0, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 0, 0,
    1, 0, 1, 0, 1, 1, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 0,
    1, 0, 0, 1, 1, 1, 1, 0,
    0, 0, 1, 0, 1, 0, 0, 0,
    0, 1, 1, 0, 0, 0, 0, 0,
    1, 0, 0, 0, 0, 0, 0,
};

static void frame_descramble(uint8_t *fr_data_tmp, const uint8_t *fr_data,
        int scr)
{
    if (scr == 0) {
        memcpy(fr_data_tmp, fr_data, FRAME_DATA_LEN);
        return;
    }

    for(int k = 0 ; k < FRAME_DATA_LEN; k++) {
        fr_data_tmp[k] = fr_data[k] ^ scramb_table[(k + scr) % 127];
    }
}

/**
  PAS 0001-2 6.1.4.2
  PAS 0001-2 6.2.4.2

  Audio and data frame differencial precoding index table was generated by the
  following python 3 scipt.

  pre_cod = ( 7, 10, 13, 16, 19, 22, 25, 28, 31, 34, 37, 40,
             43, 46, 49, 52, 55, 58, 61, 64, 67, 70, 73, 76,
             83, 86, 89, 92, 95, 98, 101, 104, 107, 110, 113, 116,
            119, 122, 125, 128, 131, 134, 137, 140, 143, 146, 149 )
  for i in range(152):
      print(1+ (i in pre_cod), end=", ")
      if i % 8 == 7:
          print()
*/
static const int diff_precod_UHF[] = {
    1, 1, 1, 1, 1, 1, 1, 2,
    1, 1, 2, 1, 1, 2, 1, 1,
    2, 1, 1, 2, 1, 1, 2, 1,
    1, 2, 1, 1, 2, 1, 1, 2,
    1, 1, 2, 1, 1, 2, 1, 1,
    2, 1, 1, 2, 1, 1, 2, 1,
    1, 2, 1, 1, 2, 1, 1, 2,
    1, 1, 2, 1, 1, 2, 1, 1,
    2, 1, 1, 2, 1, 1, 2, 1,
    1, 2, 1, 1, 2, 1, 1, 1,
    1, 1, 1, 2, 1, 1, 2, 1,
    1, 2, 1, 1, 2, 1, 1, 2,
    1, 1, 2, 1, 1, 2, 1, 1,
    2, 1, 1, 2, 1, 1, 2, 1,
    1, 2, 1, 1, 2, 1, 1, 2,
    1, 1, 2, 1, 1, 2, 1, 1,
    2, 1, 1, 2, 1, 1, 2, 1,
    1, 2, 1, 1, 2, 1, 1, 2,
    1, 1, 2, 1, 1, 2, 1, 1,
};

static void frame_diff_dec(uint8_t *fr_data)
{
    for (int j = FRAME_DATA_LEN - 1; j > 0; --j) {
        fr_data[j] ^= fr_data[j - diff_precod_UHF[j]];
    }
}

/**
  PAS 0001-2 6.1.3.1
  Generated by following python3 scritp.

p = { 0: 0, 1: 4, 2: 2, 3: 6, 4: 1, 5: 5, 6: 3, 7: 7, }
for j in range(0, 152):
    k = 19 * p[j % 8] + (3 * (j // 8)) % 19
    print(k, end=', ')
    if j % 8 == 7:
        print()
  **/
static const uint8_t interleave_voice_VHF[] = {
    0, 76, 38, 114, 19, 95, 57, 133,
    3, 79, 41, 117, 22, 98, 60, 136,
    6, 82, 44, 120, 25, 101, 63, 139,
    9, 85, 47, 123, 28, 104, 66, 142,
    12, 88, 50, 126, 31, 107, 69, 145,
    15, 91, 53, 129, 34, 110, 72, 148,
    18, 94, 56, 132, 37, 113, 75, 151,
    2, 78, 40, 116, 21, 97, 59, 135,
    5, 81, 43, 119, 24, 100, 62, 138,
    8, 84, 46, 122, 27, 103, 65, 141,
    11, 87, 49, 125, 30, 106, 68, 144,
    14, 90, 52, 128, 33, 109, 71, 147,
    17, 93, 55, 131, 36, 112, 74, 150,
    1, 77, 39, 115, 20, 96, 58, 134,
    4, 80, 42, 118, 23, 99, 61, 137,
    7, 83, 45, 121, 26, 102, 64, 140,
    10, 86, 48, 124, 29, 105, 67, 143,
    13, 89, 51, 127, 32, 108, 70, 146,
    16, 92, 54, 130, 35, 111, 73, 149,
};

// PAS 0001-2 6.1.4.1
static const uint8_t interleave_voice_UHF[] = {
    1, 77, 38, 114, 20, 96, 59, 135,
    3, 79, 41, 117, 23, 99, 62, 138,
    5, 81, 44, 120, 26, 102, 65, 141,
    8, 84, 47, 123, 29, 105, 68, 144,
    11, 87, 50, 126, 32, 108, 71, 147,
    14, 90, 53, 129, 35, 111, 74, 150,
    17, 93, 56, 132, 37, 113, 73, 4,
    0, 76, 40, 119, 19, 95, 58, 137,
    151, 80, 42, 115, 24, 100, 60, 133,
    12, 88, 48, 121, 30, 106, 66, 139,
    18, 91, 51, 124, 28, 104, 67, 146,
    10, 89, 52, 131, 34, 110, 70, 149,
    13, 97, 57, 130, 36, 112, 75, 148,
    6, 82, 39, 116, 16, 92, 55, 134,
    2, 78, 43, 122, 22, 98, 61, 140,
    9, 85, 45, 118, 27, 103, 63, 136,
    15, 83, 46, 125, 25, 101, 64, 143,
    7, 86, 49, 128, 31, 107, 69, 142,
    21, 94, 54, 127, 33, 109, 72, 145,
};

// PAS 0001-2 6.2.3.1
static const uint8_t *interleave_data_VHF = interleave_voice_VHF;

// PAS 0001-2 6.2.4.1
static const uint8_t interleave_data_UHF[] = {
    1, 77, 38, 114, 20, 96, 59, 135,
    3, 79, 41, 117, 23, 99, 62, 138,
    5, 81, 44, 120, 26, 102, 65, 141,
    8, 84, 47, 123, 29, 105, 68, 144,
    11, 87, 50, 126, 32, 108, 71, 147,
    14, 90, 53, 129, 35, 111, 74, 150,
    17, 93, 56, 132, 37, 112, 76, 148,
    2, 88, 40, 115, 19, 97, 58, 133,
    4, 75, 43, 118, 22, 100, 61, 136,
    7, 85, 46, 121, 25, 103, 64, 139,
    10, 82, 49, 124, 28, 106, 67, 142,
    13, 91, 52, 127, 31, 109, 73, 145,
    16, 94, 55, 130, 34, 113, 70, 151,
    0, 80, 39, 116, 21, 95, 57, 134,
    6, 78, 42, 119, 24, 98, 60, 137,
    9, 83, 45, 122, 27, 101, 63, 140,
    12, 86, 48, 125, 30, 104, 66, 143,
    15, 89, 51, 128, 33, 107, 69, 146,
    18, 92, 54, 131, 36, 110, 72, 149,
};

/**
  Deinterleave firts part of frame (common for data and voice frames)
  */
static void frame_deinterleave1(uint8_t *fr_data_deint, const uint8_t *fr_data,
        int band)
{
    const uint8_t *int_table;

    if (band == TETRAPOL_BAND_VHF) {
        int_table = interleave_data_VHF;
    } else {
        int_table = interleave_data_UHF;
    }

    for (int j = 0; j < FRAME_DATA_LEN1; ++j) {
        fr_data_deint[j] = fr_data[int_table[j]];
    }
}

/**
  Deinterleave second part of frame (differs for data and voice frames)
  */
static void frame_deinterleave2(uint8_t *fr_data_deint, const uint8_t *fr_data,
        int band, int fr_type)
{
    const uint8_t *int_table;

    if (band == TETRAPOL_BAND_VHF) {
        if (fr_type == FRAME_TYPE_DATA) {
            int_table = interleave_data_VHF;
        } else {
            int_table = interleave_voice_VHF;
        }
    } else {
        if (fr_type == FRAME_TYPE_DATA) {
            int_table = interleave_data_UHF;
        } else {
            int_table = interleave_voice_UHF;
        }
    }

    for (int j = FRAME_DATA_LEN1; j < FRAME_DATA_LEN; ++j) {
        fr_data_deint[j] = fr_data[int_table[j]];
    }
}

// http://ghsi.de/CRC/index.php?Polynom=10010
static void mk_crc5(uint8_t *res, const uint8_t *input, int input_len)
{
    uint8_t inv;
    memset(res, 0, 5);

    for (int i = 0; i < input_len; ++i)
    {
        inv = input[i] ^ res[0];

        res[0] = res[1];
        res[1] = res[2];
        res[2] = res[3] ^ inv;
        res[3] = res[4];
        res[4] = inv;
    }
}

// http://ghsi.de/CRC/index.php?Polynom=1010
static void mk_crc3(uint8_t *res, const uint8_t *input, int input_len)
{
    uint8_t inv;
    memset(res, 0, 3);

    for (int i = 0; i < input_len; ++i)
    {
        inv = input[i] ^ res[0];

        res[0] = res[1];
        res[1] = res[2] ^ inv;
        res[2] = inv;
    }
    res[0] = res[0] ^ 1;
    res[1] = res[1] ^ 1;
    res[2] = res[2] ^ 1;
}

/**
  PAS 0001-2 6.1.2
  PAS 0001-2 6.2.2
  */
static int decode_data_frame(uint8_t *fr_sol, uint8_t *fr_errs,
        const uint8_t *fr_data, int sol_len)
{
#ifdef FRBIT
#error "Collision in definition of macro FRBIT!"
#endif
#define FRBIT(x, y) fr_data[((x) + (y)) % (2*sol_len)]

    int nerrs = 0;
    for (int i = 0; i < sol_len; ++i) {
        fr_sol[i] = FRBIT(2*i, 2) ^ FRBIT(2*i, 3);
        const uint8_t sol2 = FRBIT(2*i, 5) ^ FRBIT(2*i, 6) ^ FRBIT(2*i, 7);
        // we have 2 solutions, check if they match
        fr_errs[i] = fr_sol[i] ^ sol2;
        nerrs += fr_errs[i];
    }
#undef FRBIT

    return nerrs;
}

static int frame_decode1(uint8_t *fr_sol, uint8_t *fr_errs,
        const uint8_t *fr_data, frame_type_t fr_type)
{
    switch (fr_type) {
        case FRAME_TYPE_AUTO:
        case FRAME_TYPE_DATA:
        case FRAME_TYPE_VOICE:
            // decode 52 bits of frame common to both types
            return decode_data_frame(fr_sol, fr_errs, fr_data, 26);
        break;

        default:
            // TODO
            LOG(ERR, "decoding frame type %d not implemented", fr_type);
            return INT_MAX;
    }
}

static int frame_decode2(uint8_t *fr_sol, uint8_t *fr_errs,
        const uint8_t *fr_data, frame_type_t fr_type)
{
    int nerrs = 0;

    switch (fr_type) {
        case FRAME_TYPE_VOICE:
            memcpy(fr_sol + 26, fr_data + 2*26, 100);
            break;

        case FRAME_TYPE_DATA:
            // decode remaining part of frame
            nerrs += decode_data_frame(fr_sol + 26, fr_errs + 26, fr_data + 2*26, 50);
            if (!nerrs && ( fr_sol[74] || fr_sol[75] )) {
                LOG(WTF, "nonzero padding in frame: %d %d",
                        fr_sol[74], fr_sol[75]);
            }
        break;

        default:
            LOG(ERR, "decoding frame type %d not implemented", fr_type);
            nerrs = INT_MAX;
    }

    return nerrs;
}

static bool frame_check_crc(const uint8_t *fr_data, frame_type_t fr_type)
{
    if (fr_type == FRAME_TYPE_AUTO) {
        fr_type = fr_data[0];
    } else {
        if (fr_type != fr_data[0]) {
            return false;
        }
    }

    if (fr_type == FRAME_TYPE_DATA) {
        uint8_t crc[5];

        mk_crc5(crc, fr_data, 69);
        return !memcmp(fr_data + 69, crc, 5);
    }

    if (fr_type == FRAME_TYPE_VOICE) {
        uint8_t crc[3];

        mk_crc3(crc, fr_data, 23);
        return !memcmp(fr_data + 23, crc, 3);
    }
    return false;
}

frame_decoder_t *frame_decoder_create(int band, int scr, int fr_type)
{
    frame_decoder_t *fd = malloc(sizeof(frame_decoder_t));
    if (!fd) {
        return NULL;
    }

    frame_decoder_reset(fd, band, scr, fr_type);

    return fd;
}

void frame_decoder_destroy(frame_decoder_t *fd)
{
    free(fd);
}

void frame_decoder_reset(frame_decoder_t *fd, int band, int scr, int fr_type)
{
    fd->band = band;
    fd->scr = scr;
    fd->fr_type = fr_type;
}

void frame_decoder_set_scr(frame_decoder_t *fd, int scr)
{
    fd->scr = scr;
}

/**
  Fix some errors in frame. This routine is pretty naive, suboptimal and does
  not use all possible potential of error correction.
  TODO, FIXME ...

  For those who want to improve it (or rather write new one from scratch!).
  Each sigle bit error in fr_data can be found by characteristic pattern
  (syndrome) in fr_errs and fixed by inverting invalid bits in fr_data.
  Trivial syndromes are are 101 and 111, both can be fixed by xoring
  fr_data with pattern 001 (xor last bit of syndrome).

  If more errors occures the resulting syndrome is linear combination of
  two trivial syndromes, the same stay for correction.

Example for 2 bit error:
syndrome    correction
  101       001
    111       001
  10011     00101
  */
int frame_fix_errs(uint8_t *fr_data, uint8_t *fr_errs, int len, int *bits_fixed)
{
    int nerrs = 0;
    for (int i = 0; i < len; ++i) {
        // Fixe some 3 bit error with 5 bit syndromes. There is few cases of
        // those even for single bite error in demodulated frame.
        if (!(fr_errs[i] |
                    (fr_errs[(i + 1) % len]) |
                    (fr_errs[(i + 2) % len]) |
                    (fr_errs[(i + 3) % len]) |
                    (fr_errs[(i + 4) % len] ^ 1) |
                    //
                    (fr_errs[(i + 8) % len] ^ 1) |
                    (fr_errs[(i + 9) % len]) |
                    (fr_errs[(i + 10) % len]) |
                    (fr_errs[(i + 11) % len]) |
                    (fr_errs[(i + 12) % len])
             )) {
            nerrs += 2 + fr_errs[(i + 5) % len] +
                fr_errs[(i + 6) % len] +
                fr_errs[(i + 7) % len];
            *bits_fixed += 2 + fr_errs[(i + 6) % len];
            fr_data[(i + 6) % len] ^= 1;
            fr_data[(i + 7) % len] ^= fr_errs[(i + 6) % len];
            fr_data[(i + 8) % len] ^= 1;
            fr_errs[(i + 4) % len] = 0;
            fr_errs[(i + 5) % len] = 0;
            fr_errs[(i + 6) % len] = 0;
            fr_errs[(i + 7) % len] = 0;
            fr_errs[(i + 8) % len] = 0;
            i += 6;
            continue;
        }
        // Fix some 2 bit errors with 4 bit syndromes.
        if (!(fr_errs[i] |
                (fr_errs[(i + 1) % len]) |
                (fr_errs[(i + 2) % len]) |
                (fr_errs[(i + 3) % len] ^ 1) |
                //
                (fr_errs[(i + 6) % len] ^ 1) |
                (fr_errs[(i + 7) % len]) |
                (fr_errs[(i + 8) % len]) |
                (fr_errs[(i + 9) % len])
             )) {
            nerrs += 2 + (fr_errs[(i + 4) % len]) + (fr_errs[(i + 5) % len]);
            *bits_fixed += 2;
            fr_data[(i + 5) % len] ^= 1;
            fr_data[(i + 6) % len] ^= 1;
            fr_errs[(i + 3) % len] = 0;
            fr_errs[(i + 4) % len] = 0;
            fr_errs[(i + 5) % len] = 0;
            fr_errs[(i + 6) % len] = 0;
            i += 4;
            continue;
        }
        // Fix some 1 bit error with 3 bit syndromes.
        if (!(fr_errs[i] |
                (fr_errs[(i + 1) % len]) |
                (fr_errs[(i + 2) % len] ^ 1) |
                //
                (fr_errs[(i + 4) % len] ^ 1) |
                (fr_errs[(i + 5) % len]) |
                (fr_errs[(i + 6) % len])
             )) {
            nerrs += 2 + (fr_errs[(i + 3) % len]);
            *bits_fixed += 1;
            fr_data[(i + 4) % len] ^= 1;
            fr_errs[(i + 2) % len] = 0;
            fr_errs[(i + 3) % len] = 0;
            fr_errs[(i + 4) % len] = 0;
            i += 4;
            continue;
        }
    }

    return nerrs;
}

/**
  Fix errors in frame, using the Viterbi algorithm.
  */
int frame_viterbi(uint8_t *dec,const uint8_t *in_bits,int size)
{
  static unsigned char viterbi_table[8]={0,3,1,2,3,0,2,1};
  int mi=999;
  for(int s=0;s<4;s++) {
    int back[size][4];
    int tab[4],tab2[4];
    memset(back,-1,sizeof(back));
    for(int i=0;i<4;i++)
      tab[i]=9999;
    tab[s]=0;
    for(int p=size-1;p>=0;p--) {
      for(int i=0;i<4;i++) tab2[i]=9999;
      for(int u=0;u<4;u++) {
        for(int x=0;x<2;x++) {
          int v=(u<<1)|x;
          int e=viterbi_table[v];           //auto e=(encode64(v)>>4)&3;
          int r=tab[u];
          if(in_bits[2*p]!=(e&1)) r++;
          if(in_bits[2*p+1]!=((e>>1)&1)) r++;
          if(tab2[v%4]>r) {
            tab2[v%4]=r;
            back[p][v%4]=u;
          }
        }
      }
      memcpy(tab,tab2,sizeof(tab));
    }
    if(mi>tab[s]) {
      mi=tab[s];
      int z=s;
      for(int p=0;p<size;p++) {
        dec[(size+p-2)%size]=(z&1);
        z=back[p][z];
      }
    }
  }
  return mi;
}

void frame_decoder_decode(frame_decoder_t *fd, frame_t *fr, const uint8_t *fr_data)
{
    if (fd->fr_type != FRAME_TYPE_AUTO &&
            fd->fr_type != FRAME_TYPE_VOICE &&
            fd->fr_type  != FRAME_TYPE_DATA)
    {
        fr->broken = -2;
        return;
    }

    fr->bits_fixed = 0;

    uint8_t fr_data_tmp[FRAME_DATA_LEN];

    frame_descramble(fr_data_tmp, fr_data, fd->scr);
    if (fd->band == TETRAPOL_BAND_UHF) {
        frame_diff_dec(fr_data_tmp);
    }

    uint8_t fr_data_deint[FRAME_DATA_LEN];
#if 0
    uint8_t fr_errs[FRAME_DATA_LEN];

    frame_deinterleave1(fr_data_deint, fr_data_tmp, fd->band);
    fr->broken = frame_decode1(fr->blob_, fr_errs, fr_data_deint, fd->fr_type);
    fr->syndromes = fr->broken;

    fr->fr_type = (fd->fr_type == FRAME_TYPE_AUTO) ? fr->d : fd->fr_type;

    if (fr->broken) {
        fr->broken -= frame_fix_errs(fr->blob_, fr_errs, 26, &fr->bits_fixed);
        if (fr->broken > 0) {
            return;
        }
    }

    frame_deinterleave2(fr_data_deint, fr_data_tmp, fd->band, fr->fr_type);
    fr->broken = frame_decode2(fr->blob_, fr_errs, fr_data_deint, fr->fr_type);
    fr->syndromes += fr->broken;

    if (fr->fr_type == FRAME_TYPE_VOICE) {
        fr->broken = frame_check_crc(fr->blob_, fr->fr_type) ? 0 : -1;
        return;
    }

    if (fr->broken) {
        fr->broken -= frame_fix_errs(fr->blob_ + 26, fr_errs + 26, 50, &fr->bits_fixed);
        if (fr->broken > 0) {
            return;
        }
    }

#else
    fr->broken=0;
    frame_deinterleave1(fr_data_deint, fr_data_tmp, fd->band);
    int f1=frame_viterbi(fr->blob_,fr_data_deint,26);
    fr->bits_fixed+=f1;
    if(f1>=6) fr->broken=1; //if too many bits are fixed, we suppose that the packet is broken

    fr->fr_type = (fd->fr_type == FRAME_TYPE_AUTO) ? (frame_type_t)fr->d : fd->fr_type;

    frame_deinterleave2(fr_data_deint, fr_data_tmp, fd->band, fr->fr_type);
    if (fr->broken==0 && fr->fr_type != FRAME_TYPE_VOICE) {
      int f2=frame_viterbi(fr->blob_+26,fr_data_deint+52,50);
      if(f2>=11) fr->broken=1;
      fr->bits_fixed+=f2;
    }
    else {
      memcpy(fr->blob_+26,fr_data_deint+52,100);
    }
    if(fr->broken) return;
#endif

    fr->broken = frame_check_crc(fr->blob_, fr->fr_type) ? 0 : -1;
}

frame_encoder_t *frame_encoder_create(int band, int scr, int dir)
{
    frame_encoder_t *fe = malloc(sizeof(frame_encoder_t));
    if (!fe) {
        return NULL;
    }

    fe->band = band;
    fe->scr = scr;
    fe->dir = dir;
    fe->first_bit = 0;

    return fe;
}

void frame_encoder_destroy(frame_encoder_t *fe)
{
    free(fe);
}

void frame_encoder_set_scr(frame_encoder_t *fe, int scr)
{
    fe->scr = scr;
}

/** Pack bits, duplicate each bit. This prepares data for encoding. */
static uint64_t pack_2x4b_64(const uint8_t *bits, int len)
{
    uint64_t val = 0;
    while (len) {
        --len;
        val <<= 2;
        val |= bits[len] * 0x03;
    }

    return val;
}

// PAS 0001-2 6.1.2 - protected part
// PAS 0001-2 6.2.2 - protected part
static void frame_encode1(uint8_t *out_bytes, const uint8_t *in_bits)
{
    uint64_t data = pack_2x4b_64(in_bits, 26);

    // create data shifted by 1 and 2 (double)bits
    uint64_t data_1 = data << 2;
    uint64_t data_2 = data << 4;
    // fill gaps with bits from frame end
    data_1 |= data >> (25 * 2);
    data_2 |= data >> (24 * 2);

    // drop one bit copy from data_1
    data_1 &= 0x5555555555555555LL;

    *(uint64_t *)out_bytes = htole64(data ^ data_1 ^ data_2);
}

/**
  Encode second part of data for data frame. Bits already encoded by
  frame_encode1 are skipped.
  */
static void frame_encode2(uint8_t *out_bytes, const uint8_t *in_bits)
{
    in_bits += 26;

    uint64_t data_a = pack_2x4b_64(in_bits, 18);
    uint64_t data_b = pack_2x4b_64(in_bits + 18, 32);

    // create data with shifted by -1 and -2
    uint64_t data_a_1 = data_a << 2;
    uint64_t data_b_1 = data_b << 2;
    uint64_t data_a_2 = data_a << 4;
    uint64_t data_b_2 = data_b << 4;

    data_a_1 |= data_b >> (2*32 - 2);
    data_a_2 |= data_b >> (2*32 - 4);
    data_b_1 |= data_a >> (2*18 - 2);
    data_b_2 |= data_a >> (2*18 - 4);

    // drop one bit copy from data_*_1
    data_a_1 &= 0x5555555555555555LL;
    data_b_1 &= 0x5555555555555555LL;

    uint64_t data_ra = htole64((data_a ^ data_a_1 ^ data_a_2) & ((1LL << (2*18)) - 1));
    uint64_t data_rb = htole64(data_b ^ data_b_1 ^ data_b_2);
    *(uint64_t *)(&out_bytes[2*26/8]) |= data_ra << 4;
    *(uint64_t *)(&out_bytes[(2*26+2*18)/8]) |= data_rb;
}

static void frame_interleave(uint8_t *out, const uint8_t *in,
        const uint8_t *int_table)
{
    memset(out, 0, FRAME_DATA_LEN / 8);
    for (uint8_t j = 0; j < FRAME_DATA_LEN; ++j) {
        uint8_t k = int_table[j];
        const uint8_t val = (in[j / 8] >> (j % 8)) & 1;
        out[k / 8] |= val << (k % 8);
    }
}

static void frame_diff_enc(uint8_t *fr_data)
{
    for (int j = 1; j < FRAME_DATA_LEN; ++j) {
        const uint8_t j_ = j - diff_precod_UHF[j];
        const uint8_t val = (fr_data[j_ / 8] >> (j_ % 8)) & 1;
        fr_data[j / 8] ^= val << (j % 8);
    }
}

/**
  PAS 0001-2 6.1.5.1
  PAS 0001-2 6.2.5.1
  PAS 0001-2 6.3.4.1

  Scrambling sequence tables have been generated by this python3 script

s = [1, 1, 1, 1, 1, 1, 1]
for k in range(len(s), (19+16+1)*8):
    s.append(s[k-1] ^ s[k-7])

for i in range(8):
    print('{ \n    ', end='')
    for j in range(19+16):
        v = s[8*j+i:8*j+8+i]
        v.reverse()
        v = '0b' + ''.join([str(c) for c in v])
        v = eval(v)
        print("0x%02x, " % v, end='')
        if j % 8 == 7:
            print('\n    ', end='')
    print(' },')
  */
static uint8_t scramb_tables[8][19+16] = {
{
    0x7f, 0x95, 0xb9, 0x4b, 0x63, 0x6f, 0x6d, 0x12,
    0x87, 0x3e, 0x75, 0x16, 0x79, 0x14, 0x06, 0x81,
    0xbf, 0xca, 0xdc, 0xa5, 0xb1, 0xb7, 0x36, 0x89,
    0x43, 0x9f, 0x3a, 0x8b, 0x3c, 0x0a, 0x83, 0xc0,
    0x5f, 0x65, 0xee,  },
{
    0xbf, 0xca, 0xdc, 0xa5, 0xb1, 0xb7, 0x36, 0x89,
    0x43, 0x9f, 0x3a, 0x8b, 0x3c, 0x0a, 0x83, 0xc0,
    0x5f, 0x65, 0xee, 0xd2, 0xd8, 0x5b, 0x9b, 0xc4,
    0xa1, 0x4f, 0x9d, 0x45, 0x1e, 0x85, 0x41, 0xe0,
    0xaf, 0x32, 0x77,  },
{
    0x5f, 0x65, 0xee, 0xd2, 0xd8, 0x5b, 0x9b, 0xc4,
    0xa1, 0x4f, 0x9d, 0x45, 0x1e, 0x85, 0x41, 0xe0,
    0xaf, 0x32, 0x77, 0x69, 0xec, 0xad, 0x4d, 0xe2,
    0xd0, 0xa7, 0xce, 0x22, 0x8f, 0xc2, 0x20, 0xf0,
    0x57, 0x99, 0xbb,  },
{
    0xaf, 0x32, 0x77, 0x69, 0xec, 0xad, 0x4d, 0xe2,
    0xd0, 0xa7, 0xce, 0x22, 0x8f, 0xc2, 0x20, 0xf0,
    0x57, 0x99, 0xbb, 0x34, 0xf6, 0xd6, 0x26, 0x71,
    0xe8, 0x53, 0x67, 0x91, 0x47, 0x61, 0x10, 0xf8,
    0xab, 0xcc, 0x5d,  },
{
    0x57, 0x99, 0xbb, 0x34, 0xf6, 0xd6, 0x26, 0x71,
    0xe8, 0x53, 0x67, 0x91, 0x47, 0x61, 0x10, 0xf8,
    0xab, 0xcc, 0x5d, 0x1a, 0x7b, 0x6b, 0x93, 0x38,
    0xf4, 0xa9, 0xb3, 0xc8, 0xa3, 0x30, 0x08, 0xfc,
    0x55, 0xe6, 0x2e,  },
{
    0xab, 0xcc, 0x5d, 0x1a, 0x7b, 0x6b, 0x93, 0x38,
    0xf4, 0xa9, 0xb3, 0xc8, 0xa3, 0x30, 0x08, 0xfc,
    0x55, 0xe6, 0x2e, 0x8d, 0xbd, 0xb5, 0x49, 0x1c,
    0xfa, 0xd4, 0x59, 0xe4, 0x51, 0x18, 0x04, 0xfe,
    0x2a, 0x73, 0x97,  },
{
    0x55, 0xe6, 0x2e, 0x8d, 0xbd, 0xb5, 0x49, 0x1c,
    0xfa, 0xd4, 0x59, 0xe4, 0x51, 0x18, 0x04, 0xfe,
    0x2a, 0x73, 0x97, 0xc6, 0xde, 0xda, 0x24, 0x0e,
    0x7d, 0xea, 0x2c, 0xf2, 0x28, 0x0c, 0x02, 0x7f,
    0x95, 0xb9, 0x4b,  },
{
    0x2a, 0x73, 0x97, 0xc6, 0xde, 0xda, 0x24, 0x0e,
    0x7d, 0xea, 0x2c, 0xf2, 0x28, 0x0c, 0x02, 0x7f,
    0x95, 0xb9, 0x4b, 0x63, 0x6f, 0x6d, 0x12, 0x87,
    0x3e, 0x75, 0x16, 0x79, 0x14, 0x06, 0x81, 0xbf,
    0xca, 0xdc, 0xa5,  },
    };

static void frame_scramble(uint8_t *fr_data, int scr)
{
    if (scr == 0) {
        return;
    }

    const uint8_t *scr_table = scramb_tables[scr % 8];
    scr /= 8;

    for (uint8_t i = 0; i < FRAME_DATA_LEN / 8; ++i) {
        fr_data[i] ^= scr_table[i+scr];
    }
}

static uint8_t differential_enc(uint8_t *data, int size, uint8_t first_bit)
{
    for (int i = 0; i < size; ++i) {
        uint8_t top = data[i] >> 7;
        data[i] ^= (data[i] << 1) | first_bit;
        first_bit = top;
    }

    return first_bit;
}

static int encode_data(frame_encoder_t *fe, uint8_t *fr_data, frame_t *fr)
{
    fr->data.d = 1;
    mk_crc5(fr->data.crc, fr->data.crc_data, sizeof(fr->data.crc_data));

    memset(fr->data.zero, 0, sizeof(fr->data.zero));

    uint8_t buf[19];
    memset(buf, 0, sizeof(buf));
    frame_encode1(buf, fr->blob_);
    frame_encode2(buf, fr->blob_);

    if (fe->band == TETRAPOL_BAND_VHF) {
        frame_interleave(&fr_data[1], buf, interleave_data_VHF);
    } else if (fe->band == TETRAPOL_BAND_UHF) {
        frame_interleave(&fr_data[1], buf, interleave_data_UHF);
    } else {
        return -1;
    }

    if (fe->band == TETRAPOL_BAND_UHF) {
        frame_diff_enc(&fr_data[1]);
    }

    frame_scramble(&fr_data[1], fe->scr);

    // PAS 0001-2 6.2.5.2
    fr_data[0] = 0x46;

    fe->first_bit = differential_enc(fr_data, 20, fe->first_bit);

    return 0;
}

static int encode_voice(frame_encoder_t *fe, uint8_t *fr_data, frame_t *fr)
{
    fr->voice.d = 0;
    mk_crc3(fr->voice.crc, fr->voice.crc_data, sizeof(fr->voice.crc_data));

    uint8_t buf[19];
    memset(buf, 0, sizeof(buf));
    frame_encode1(buf, fr->voice.crc_data);
    // PAS 0001-2 6.2.1 - unprotected part
    pack_bits(buf, fr->voice.voice2, 2*26, 100);

    if (fe->band == TETRAPOL_BAND_VHF) {
        frame_interleave(&fr_data[1], buf, interleave_voice_VHF);
    } else if (fe->band == TETRAPOL_BAND_UHF) {
        frame_interleave(&fr_data[1], buf, interleave_voice_UHF);
    } else {
        return -1;
    }

    if (fe->band == TETRAPOL_BAND_UHF) {
        frame_diff_enc(&fr_data[1]);
    }

    frame_scramble(&fr_data[1], fe->scr);

    // PAS 0001-2 6.1.5.2
    fr_data[0] = 0x46;

    fe->first_bit = differential_enc(fr_data, 20, fe->first_bit);

    return 0;
}

int frame_encoder_encode(frame_encoder_t *fe, uint8_t *fr_data, frame_t *fr)
{
    if (fr->fr_type == FRAME_TYPE_DATA) {
        return encode_data(fe, fr_data, fr);;
    } else if (fr->fr_type == FRAME_TYPE_VOICE) {
        return encode_voice(fe, fr_data, fr);
    }

    return -1;
}


