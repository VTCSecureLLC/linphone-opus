/* Copyright (C) 2008 Jean-Marc Valin, CSIRO
   File: celtdec.c

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:
   
   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
   
   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
   
   - Neither the name of the Xiph.org Foundation nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.
   
   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdio.h>
#if !defined WIN32 && !defined _WIN32
#include <unistd.h>
#endif
#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif
#ifndef HAVE_GETOPT_LONG
#include "getopt_win.h"
#endif
#include <stdlib.h>
#include <string.h>

#include <celt.h>
#include <ogg/ogg.h>

#if defined WIN32 || defined _WIN32
#include "wave_out.h"
/* We need the following two to set stdout to binary */
#include <io.h>
#include <fcntl.h>
#endif
#include <math.h>

#ifdef __MINGW32__
#include "wave_out.c"
#endif

#ifdef HAVE_SYS_SOUNDCARD_H
#include <sys/soundcard.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>

#elif defined HAVE_SYS_AUDIOIO_H
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/audioio.h>
#ifndef AUDIO_ENCODING_SLINEAR
#define AUDIO_ENCODING_SLINEAR AUDIO_ENCODING_LINEAR /* Solaris */
#endif

#endif

#include <string.h>
#include "wav_io.h"
#include <celt_header.h>

#define MAX_FRAME_SIZE 2000

#define readint(buf, base) (((buf[base+3]<<24)&0xff000000)| \
                           ((buf[base+2]<<16)&0xff0000)| \
                           ((buf[base+1]<<8)&0xff00)| \
  	           	    (buf[base]&0xff))

static void print_comments(char *comments, int length)
{
   char *c=comments;
   int len, i, nb_fields;
   char *end;
   
   if (length<8)
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   end = c+length;
   len=readint(c, 0);
   c+=4;
   if (c+len>end)
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   fwrite(c, 1, len, stderr);
   c+=len;
   fprintf (stderr, "\n");
   if (c+4>end)
   {
      fprintf (stderr, "Invalid/corrupted comments\n");
      return;
   }
   nb_fields=readint(c, 0);
   c+=4;
   for (i=0;i<nb_fields;i++)
   {
      if (c+4>end)
      {
         fprintf (stderr, "Invalid/corrupted comments\n");
         return;
      }
      len=readint(c, 0);
      c+=4;
      if (c+len>end)
      {
         fprintf (stderr, "Invalid/corrupted comments\n");
         return;
      }
      fwrite(c, 1, len, stderr);
      c+=len;
      fprintf (stderr, "\n");
   }
}

FILE *out_file_open(char *outFile, int rate, int *channels)
{
   FILE *fout=NULL;
   /*Open output file*/
   if (strlen(outFile)==0)
   {
#if defined HAVE_SYS_SOUNDCARD_H
      int audio_fd, format, stereo;
      audio_fd=open("/dev/dsp", O_WRONLY);
      if (audio_fd<0)
      {
         perror("Cannot open /dev/dsp");
         exit(1);         
      }

      format=AFMT_S16_NE;
      if (ioctl(audio_fd, SNDCTL_DSP_SETFMT, &format)==-1)
      {
         perror("SNDCTL_DSP_SETFMT");
         close(audio_fd);
         exit(1);
      }

      stereo=0;
      if (*channels==2)
         stereo=1;
      if (ioctl(audio_fd, SNDCTL_DSP_STEREO, &stereo)==-1)
      {
         perror("SNDCTL_DSP_STEREO");
         close(audio_fd);
         exit(1);
      }
      if (stereo!=0)
      {
         if (*channels==1)
            fprintf (stderr, "Cannot set mono mode, will decode in stereo\n");
         *channels=2;
      }

      if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &rate)==-1)
      {
         perror("SNDCTL_DSP_SPEED");
         close(audio_fd);
         exit(1);
      }
      fout = fdopen(audio_fd, "w");
#elif defined HAVE_SYS_AUDIOIO_H
      audio_info_t info;
      int audio_fd;
      
      audio_fd = open("/dev/audio", O_WRONLY);
      if (audio_fd<0)
      {
         perror("Cannot open /dev/audio");
         exit(1);
      }

      AUDIO_INITINFO(&info);
#ifdef AUMODE_PLAY    /* NetBSD/OpenBSD */
      info.mode = AUMODE_PLAY;
#endif
      info.play.encoding = AUDIO_ENCODING_SLINEAR;
      info.play.precision = 16;
      info.play.sample_rate = rate;
      info.play.channels = *channels;
      
      if (ioctl(audio_fd, AUDIO_SETINFO, &info) < 0)
      {
         perror ("AUDIO_SETINFO");
         exit(1);
      }
      fout = fdopen(audio_fd, "w");
#elif defined WIN32 || defined _WIN32
      {
         unsigned int celt_channels = *channels;
         if (Set_WIN_Params (INVALID_FILEDESC, rate, SAMPLE_SIZE, celt_channels))
         {
            fprintf (stderr, "Can't access %s\n", "WAVE OUT");
            exit(1);
         }
      }
#else
      fprintf (stderr, "No soundcard support\n");
      exit(1);
#endif
   } else {
      if (strcmp(outFile,"-")==0)
      {
#if defined WIN32 || defined _WIN32
         _setmode(_fileno(stdout), _O_BINARY);
#endif
         fout=stdout;
      }
      else 
      {
         fout = fopen(outFile, "wb");
         if (!fout)
         {
            perror(outFile);
            exit(1);
         }
         if (strcmp(outFile+strlen(outFile)-4,".wav")==0 || strcmp(outFile+strlen(outFile)-4,".WAV")==0)
            write_wav_header(fout, rate, *channels, 0, 0);
      }
   }
   return fout;
}

void usage()
{
   printf ("Usage: celtdec [options] input_file.spx [output_file]\n");
   printf ("\n");
   printf ("Decodes a CELT file and produce a WAV file or raw file\n");
   printf ("\n");
   printf ("input_file can be:\n");
   printf ("  filename.oga         regular CELT file\n");
   printf ("  -                    stdin\n");
   printf ("\n");  
   printf ("output_file can be:\n");
   printf ("  filename.wav         Wav file\n");
   printf ("  filename.*           Raw PCM file (any extension other that .wav)\n");
   printf ("  -                    stdout\n");
   printf ("  (nothing)            Will be played to soundcard\n");
   printf ("\n");  
   printf ("Options:\n");
   printf (" --mono                Force decoding in mono\n");
   printf (" --stereo              Force decoding in stereo\n");
   printf (" --rate n              Force decoding at sampling rate n Hz\n");
   printf (" --packet-loss n       Simulate n %% random packet loss\n");
   printf (" -V                    Verbose mode (show bit-rate)\n"); 
   printf (" -h, --help            This help\n");
   printf (" -v, --version         Version information\n");
   printf ("\n");
}

void version()
{
   printf ("celtenc (CELT encoder)\n");
   printf ("Copyright (C) 2008 Jean-Marc Valin\n");
}

void version_short()
{
   printf ("celtenc (CELT encoder)\n");
   printf ("Copyright (C) 2008 Jean-Marc Valin\n");
}

static CELTDecoder *process_header(ogg_packet *op, celt_int32_t enh_enabled, celt_int32_t *frame_size, int *granule_frame_size, celt_int32_t *rate, int *nframes, int forceMode, int *channels, int *extra_headers, int quiet)
{
   CELTDecoder *st;
   const CELTMode *mode;
   CELTHeader header;
   int modeID;
      
   celt_header_from_packet((char*)op->packet, op->bytes, &header);
      
   if (header.mode==0)
      mode = celt_mono;
   else if (header.mode==1)
      mode = celt_stereo;
   else
   {
      fprintf (stderr, "Invalid mode: %d\n", header.mode);
      return NULL;
   }

   st = celt_decoder_new(mode);
   if (!st)
   {
      fprintf (stderr, "Decoder initialization failed.\n");
      return NULL;
   }
   
   celt_mode_info(mode, CELT_GET_FRAME_SIZE, frame_size);
   *granule_frame_size = *frame_size;

   if (!*rate)
      *rate = header.sample_rate;
   /* Adjust rate if --force-* options are used */

   *nframes = 1;

   if (*channels==-1)
      *channels = header.nb_channels;
   
   if (!quiet)
   {
      fprintf (stderr, "Decoding %d Hz audio in", *rate);

      if (*channels==1)
         fprintf (stderr, " (mono");
      else
         fprintf (stderr, " (stereo");
      fprintf(stderr, ")\n");
   }

   *extra_headers = header.extra_headers;

   return st;
}

int main(int argc, char **argv)
{
   int c;
   int option_index = 0;
   char *inFile, *outFile;
   FILE *fin, *fout=NULL;
   short out[MAX_FRAME_SIZE];
   short output[MAX_FRAME_SIZE];
   int frame_size=0, granule_frame_size=0;
   void *st=NULL;
   unsigned char bits[1000];
   int packet_count=0;
   int stream_init = 0;
   int quiet = 0;
   ogg_int64_t page_granule=0, last_granule=0;
   int skip_samples=0, page_nb_packets;
   struct option long_options[] =
   {
      {"help", no_argument, NULL, 0},
      {"quiet", no_argument, NULL, 0},
      {"version", no_argument, NULL, 0},
      {"version-short", no_argument, NULL, 0},
      {"rate", required_argument, NULL, 0},
      {"mono", no_argument, NULL, 0},
      {"stereo", no_argument, NULL, 0},
      {"packet-loss", required_argument, NULL, 0},
      {0, 0, 0, 0}
   };
   ogg_sync_state oy;
   ogg_page       og;
   ogg_packet     op;
   ogg_stream_state os;
   int enh_enabled;
   int nframes=2;
   int print_bitrate=0;
   int close_in=0;
   int eos=0;
   int forceMode=-1;
   int audio_size=0;
   float loss_percent=-1;
   int channels=-1;
   int rate=0;
   int extra_headers=0;
   int wav_format=0;
   int lookahead;
   int celt_serialno = -1;

   enh_enabled = 1;

   /*Process options*/
   while(1)
   {
      c = getopt_long (argc, argv, "hvV",
                       long_options, &option_index);
      if (c==-1)
         break;
      
      switch(c)
      {
      case 0:
         if (strcmp(long_options[option_index].name,"help")==0)
         {
            usage();
            exit(0);
         } else if (strcmp(long_options[option_index].name,"quiet")==0)
         {
            quiet = 1;
         } else if (strcmp(long_options[option_index].name,"version")==0)
         {
            version();
            exit(0);
         } else if (strcmp(long_options[option_index].name,"version-short")==0)
         {
            version_short();
            exit(0);
         } else if (strcmp(long_options[option_index].name,"mono")==0)
         {
            channels=1;
         } else if (strcmp(long_options[option_index].name,"stereo")==0)
         {
            channels=2;
         } else if (strcmp(long_options[option_index].name,"rate")==0)
         {
            rate=atoi (optarg);
         } else if (strcmp(long_options[option_index].name,"packet-loss")==0)
         {
            loss_percent = atof(optarg);
         }
         break;
      case 'h':
         usage();
         exit(0);
         break;
      case 'v':
         version();
         exit(0);
         break;
      case 'V':
         print_bitrate=1;
         break;
      case '?':
         usage();
         exit(1);
         break;
      }
   }
   if (argc-optind!=2 && argc-optind!=1)
   {
      usage();
      exit(1);
   }
   inFile=argv[optind];

   if (argc-optind==2)
      outFile=argv[optind+1];
   else
      outFile = "";
   wav_format = strlen(outFile)>=4 && (
                                       strcmp(outFile+strlen(outFile)-4,".wav")==0
                                       || strcmp(outFile+strlen(outFile)-4,".WAV")==0);
   /*Open input file*/
   if (strcmp(inFile, "-")==0)
   {
#if defined WIN32 || defined _WIN32
      _setmode(_fileno(stdin), _O_BINARY);
#endif
      fin=stdin;
   }
   else 
   {
      fin = fopen(inFile, "rb");
      if (!fin)
      {
         perror(inFile);
         exit(1);
      }
      close_in=1;
   }


   /*Init Ogg data struct*/
   ogg_sync_init(&oy);
   
   /*Main decoding loop*/
   
   while (1)
   {
      char *data;
      int i, j, nb_read;
      /*Get the ogg buffer for writing*/
      data = ogg_sync_buffer(&oy, 200);
      /*Read bitstream from input file*/
      nb_read = fread(data, sizeof(char), 200, fin);      
      ogg_sync_wrote(&oy, nb_read);

      /*Loop for all complete pages we got (most likely only one)*/
      while (ogg_sync_pageout(&oy, &og)==1)
      {
         int packet_no;
         if (stream_init == 0) {
            ogg_stream_init(&os, ogg_page_serialno(&og));
            stream_init = 1;
         }
	 if (ogg_page_serialno(&og) != os.serialno) {
	    /* so all streams are read. */
	    ogg_stream_reset_serialno(&os, ogg_page_serialno(&og));
	 }
         /*Add page to the bitstream*/
         ogg_stream_pagein(&os, &og);
         page_granule = ogg_page_granulepos(&og);
         page_nb_packets = ogg_page_packets(&og);
         if (page_granule>0 && frame_size)
         {
            /* FIXME: shift the granule values if --force-* is specified */
            skip_samples = frame_size*(page_nb_packets*granule_frame_size*nframes - (page_granule-last_granule))/granule_frame_size;
            if (ogg_page_eos(&og))
               skip_samples = -skip_samples;
            /*else if (!ogg_page_bos(&og))
               skip_samples = 0;*/
         } else
         {
            skip_samples = 0;
         }
         /*printf ("page granulepos: %d %d %d\n", skip_samples, page_nb_packets, (int)page_granule);*/
         last_granule = page_granule;
         /*Extract all available packets*/
         packet_no=0;
         while (!eos && ogg_stream_packetout(&os, &op) == 1)
         {
	    if (!memcmp(op.packet, "CELT    ", 8)) {
	       celt_serialno = os.serialno;
	    }
	    if (celt_serialno == -1 || os.serialno != celt_serialno)
	       break;
            /*If first packet, process as CELT header*/
            if (packet_count==0)
            {
               st = process_header(&op, enh_enabled, &frame_size, &granule_frame_size, &rate, &nframes, forceMode, &channels, &extra_headers, quiet);
               if (!st)
                  exit(1);
               //FIXME: Do that properly
               //celt_decoder_ctl(st, SPEEX_GET_LOOKAHEAD, &lookahead);
               lookahead = 0;
               if (!nframes)
                  nframes=1;
               fout = out_file_open(outFile, rate, &channels);

            } else if (packet_count==1)
            {
               if (!quiet)
                  print_comments((char*)op.packet, op.bytes);
            } else if (packet_count<=1+extra_headers)
            {
               /* Ignore extra headers */
            } else {
               int lost=0;
               packet_no++;
               if (loss_percent>0 && 100*((float)rand())/RAND_MAX<loss_percent)
                  lost=1;

               /*End of stream condition*/
               if (op.e_o_s && os.serialno == celt_serialno) /* don't care for anything except celt eos */
                  eos=1;
	       
               {
                  int ret;
                  /*Decode frame*/
                  if (!lost)
                     ret = celt_decode(st, (unsigned char*)op.packet, op.bytes, output);
                  else
                     ret = celt_decode(st, NULL, 0, output);

                  /*for (i=0;i<frame_size*channels;i++)
                    printf ("%d\n", (int)output[i]);*/

                  if (ret!=0)
                  {
                     fprintf (stderr, "Decoding error: corrupted stream?\n");
                     break;
                  }

                  if (print_bitrate) {
                     celt_int32_t tmp=40;
                     char ch=13;
                     fputc (ch, stderr);
                     fprintf (stderr, "Bitrate in use: %d bytes/packet     ", tmp);
                  }
                  /*Convert to short and save to output file*/
                  if (strlen(outFile)!=0)
                  {
                     for (i=0;i<frame_size*channels;i++)
                        out[i]=le_short(output[i]);
                  } else {
                     for (i=0;i<frame_size*channels;i++)
                        out[i]=output[i];
                  }
                  {
                     int frame_offset = 0;
                     int new_frame_size = frame_size;
                     /*printf ("packet %d %d\n", packet_no, skip_samples);*/
                     /*fprintf (stderr, "packet %d %d %d\n", packet_no, skip_samples, lookahead);*/
                     if (packet_no == 1 && j==0 && skip_samples > 0)
                     {
                        /*printf ("chopping first packet\n");*/
                        new_frame_size -= skip_samples+lookahead;
                        frame_offset = skip_samples+lookahead;
                     }
                     if (packet_no == page_nb_packets && skip_samples < 0)
                     {
                        int packet_length = nframes*frame_size+skip_samples+lookahead;
                        new_frame_size = packet_length - j*frame_size;
                        if (new_frame_size<0)
                           new_frame_size = 0;
                        if (new_frame_size>frame_size)
                           new_frame_size = frame_size;
                        /*printf ("chopping end: %d %d %d\n", new_frame_size, packet_length, packet_no);*/
                     }
                     if (new_frame_size>0)
                     {  
#if defined WIN32 || defined _WIN32
                        if (strlen(outFile)==0)
                           WIN_Play_Samples (out+frame_offset*channels, sizeof(short) * new_frame_size*channels);
                        else
#endif
                           fwrite(out+frame_offset*channels, sizeof(short), new_frame_size*channels, fout);
                  
                        audio_size+=sizeof(short)*new_frame_size*channels;
                     }
                  }
               }
            }
            packet_count++;
         }
      }
      if (feof(fin))
         break;

   }

   if (fout && wav_format)
   {
      if (fseek(fout,4,SEEK_SET)==0)
      {
         int tmp;
         tmp = le_int(audio_size+36);
         fwrite(&tmp,4,1,fout);
         if (fseek(fout,32,SEEK_CUR)==0)
         {
            tmp = le_int(audio_size);
            fwrite(&tmp,4,1,fout);
         } else
         {
            fprintf (stderr, "First seek worked, second didn't\n");
         }
      } else {
         fprintf (stderr, "Cannot seek on wave file, size will be incorrect\n");
      }
   }

   if (st)
      celt_decoder_destroy(st);
   else 
   {
      fprintf (stderr, "This doesn't look like a CELT file\n");
   }
   if (stream_init)
      ogg_stream_clear(&os);
   ogg_sync_clear(&oy);

#if defined WIN32 || defined _WIN32
   if (strlen(outFile)==0)
      WIN_Audio_close ();
#endif

   if (close_in)
      fclose(fin);
   if (fout != NULL)
      fclose(fout);   

   return 0;
}
