/* ut70d multimeter communication software

Copyright (c) 2010 Peter Popovec <popovec@fei.tuke.sk>

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
USA.

*/



/*
DEBUG switches 
-r      raw mode, send command (default  command=137) to device and print raw output 
-c<num> send command <num> to device, implies raw. If no argument, command "0" is send  
-i      if set, parse paket and print internals, imples raw




*/

#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <string.h>

static void bits (int b);
static int dumpdata (unsigned char *data, int len, FILE * fd);
static int checksum (unsigned char *d, int len);
static void parserawdata (unsigned char *d, int len);
static void printxvalue (unsigned char *d, int len, int dot);
static void printsvalue (unsigned char *d, int len);
static void printmode (unsigned char *buf, int len);
static int initserial (char *n);
static void restoreserial (int fd);
struct termios ts;		// save  to restore old parameters (or move to initserial if not needed)

int F8_range[] = { 3, 1, 2, 3, 4, 0, 0, 0 };
int F0_range[] = { 1, 2, 3, 4, 0, 0, 0, 0 };
int E8_range[] = { 2, 3, 0, 0, 0, 0, 0, 0 };
int E0_range[] = { 3, 1, 2, 3, 1, 2, 2, 0 };	//last active range is nS on 5 digit display (800nS)
int E1_range[] = { 1, 2, 3, 1, 2, 3, 0, 0 };
int D8_range[] = { 1, 0, 0, 0, 0, 0, 0, 0 };
int A8_range[] = { 1, 2, 0, 0, 0, 0, 0, 0 };
int A9_range[] = { 1, 2, 0, 0, 0, 0, 0, 0 };
int B0_range[] = { 2, 3, 0, 0, 0, 0, 0, 0 };
int B1_range[] = { 2, 3, 0, 0, 0, 0, 0, 0 };
int FAIL_range[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
int Hz_range[] = { 2, 3, 1, 2, 3, 1, 2, 3 };
int PR_range[] = { 0, 2, 0, 0, 0, 0, 0, 0 };

int *base_range;		// value * 10^base_range
int sub_range;			//

int
eeprom (int fd)
{
  int i;
  unsigned char buf[32];
  int len;

  for (i = 0; i < 1; i++)
    {
      buf[0] = 0xd0;

      buf[1] = ((i/16) & 0x0f) | 0x40;
      buf[2] = (i & 0x0f) | 0x40;
      buf[3] = 0x0a;
      
      //debug
      buf[1]=0x40;
      buf[2]=0x40;
      write (fd, buf, 4);
      usleep (100000);
      if ((len = read (fd, buf, 15)) > 0)
	  dumpdata (buf, len, stdout);
      else
	fprintf (stderr, "No response\n");
      usleep(100000);	
    }
  return 0;
}


int
main (int argc, char **argv)
{
  int fd, opt;
  unsigned char buf[15];
  unsigned char lastbuf[15];

  int raw = 0;
  unsigned char cmd = 137;

  while ((opt = getopt (argc, argv, "irc:")) != -1)
    {
      switch (opt)
	{
	case ('i'):
	  raw |= 3;
	  break;
	case ('r'):
	  raw |= 1;
	  break;
	case ('c'):
	  raw |= 1;
	  cmd = (strtol (optarg, (char **) NULL, 10)) & 255;
	  break;
	}
    }
  if (argc < 2)
    {
      fprintf (stderr, "Usage: %s <device>\n", argv[0]);
      exit (1);
    }
  fd = initserial (argv[argc - 1]);
  usleep (10000);

  if (raw)
    {
      int len;
      if ((cmd & 0xfe) == 0xd0)
	{
	  return (eeprom (fd));
	}
      buf[0] = cmd;
      write (fd, buf, 1);
      usleep (90000);
      if ((len = read (fd, buf, 15)) > 0)
	{
	  dumpdata (buf, len, stdout);
	  if (raw & 2)
	    parserawdata (buf, len);
	}
      else
	fprintf (stderr, "No response\n");

      restoreserial (fd);
      return (len <= 0);
    }
  return 0;
}

void
bits (int b)
{
  int i;
  for (i = 0; i < 8; i++)
    {
      putchar (b & 0x80 ? '1' : '0');
      b *= 2;
    }
}


int
dumpdata (unsigned char *d, int len, FILE * fd)
{
  int i,ret=0;
  unsigned char xor;
  
  printf("dump: %d\n",len);
  if (len == 1)
    return 1;
  if (len < 3){
      fprintf (stderr, "DEBUG: paket too short (%d), ignoring\n",len);
      ret++;
  }    
  if (checksum (d, len))
    {
      fprintf (stderr, "DEBUG: bad checksum, ignoring paket\n");
      ret++;
    }

  fprintf (fd, "[");
  for (xor = i = 0; i < len; i++)
    {
      if (d[i] != 0x0a)
	fprintf (fd, "%02X ", d[i]);
      else
	{
	  fprintf (fd, "0A");
	  break;
	}
    }
  fprintf (fd, "]\n");
  return ret;
}

int
checksum (unsigned char *d, int len)
{
  int i;
  unsigned char xor;
  if (len < 3)
    return 1;
  for (xor = i = 0; i < len - 2; i++)
    xor = xor ^ d[i];
  xor = xor ^ ((xor & 0xc0) / 4);
  xor &= 0x3f;
  xor = xor - d[len - 2] + 0x22;
  return xor;
}

void
parserawdata (unsigned char *d, int len)
{
  int val;
  if (len < 7)
    return;
  printmode (d, len);
  printsvalue (d, len);
  switch (d[0])
    {
      if (len != 11)
	{
	  fprintf (stderr, "DEBUG: Wrong  paket len=%d 11 wanted\n", len);
	  return;
	}
    case (128):
    case (129):
    case (130):
    case (131):
    case (132):
    case (133):
    case (134):
      val = d[5] & 0x3f;
      val *= 64;
      val |= d[6] & 0x3f;
      val *= 64;
      val |= d[7] & 0x3f;
      val *= 64;
      val |= d[8] & 0x3f;
//      if (d[5] & 20)
//      val = -((~val) & 0xffffff);
      printf ("%d <%d>", val, val / 4096);
      break;
      //unknown data  
    case (135):
    case (136):
      if (len != 15)
	{
	  fprintf (stderr, "DEBUG: Wrong  paket len=%d 15 wanted\n", len);
	  return;
	}

      printf (" {%02X%02X%02X%02X%02X%02X%02X%02X}", d[5], d[6], d[7], d[8],
	      d[9], d[10], d[11], d[12]);
      break;
      //bargraph
    case (138):
      if (len != 8)
	{
	  fprintf (stderr, "DEBUG: Wrong  paket len=%d 8 wanted\n", len);
	  return;
	}

      printf ("%d", d[5] - 0x80);
      break;
//standard read, ADC value in 4/5 positions
    case (137):
    case (139):
    case (140):
    case (141):
    case (142):
    case (143):
    case (144):
    case (145):
    case (146):
    case (147):
    case (148):
    case (150):
      if (len != 12)
	{
	  fprintf (stderr, "DEBUG: Wrong  paket len=%d 12 wanted\n", len);
	  return;
	}
      printxvalue (d, len, 0);
      break;
//read only settings
    case (149):
      if (len != 7)
	{
	  fprintf (stderr, "DEBUG: Wrong  paket len=%d 7 wanted\n", len);
	  return;
	}
      break;
    }
  printf ("\n");
}

void
printsvalue (unsigned char *d, int len)
{

  //byte2
  if (!(d[2] & 0x80))
    {
      fprintf (stderr, "DEBUG: bit 7 in byte[2] not 1: ");
      dumpdata (d, len, stderr);
    }
  printf ("%s ", d[2] & 0x40 ? "Manual" : "AUTO");
  printf ("range:%d Unit:%d ", (d[2] / 8) & 7, d[2] & 7);
  sub_range = (d[2] / 8) & 7;
  if ((d[2] & 7) == 4)
    base_range = Hz_range;
  if ((d[2] & 7) == 5)
    base_range = PR_range;

  //byte 3  bits 5,0,1 unknmown, always 0 ?
  if ((d[3] & 0x23))
    {
      fprintf (stderr, "DEBUG: bits (1,5,6) not zero in byte[3]: ");
      dumpdata (d, len, stderr);
    }
  if (!(d[3] & 0x80))
    {
      fprintf (stderr, "DEBUG: bit 7 in byte[3] not 1: ");
      dumpdata (d, len, stderr);
    }
  printf ("%s %s ", d[3] & 4 ? "BEEP" : "_", d[3] & 0x40 ? "REC" : "_");
  switch (d[3] & 0x18)
    {
    case 0:
      printf ("_");
      break;
    case (0x08):
      printf ("MAX");
      break;
    case (0x10):
      printf ("MIN");
      break;
    case (0x18):
      printf ("AVG");
      break;
    }
  //byte 4
  if ((d[4] & 0x42))
    {
      fprintf (stderr, "DEBUG: bits (1,5,6) not zero in byte[4]: ");
      dumpdata (d, len, stderr);
    }
  if (!(d[4] & 0x80))
    {
      fprintf (stderr, "DEBUG: bit 7 in byte[4] not 1: ");
      dumpdata (d, len, stderr);
    }
  printf (" %s %s %s %s %s", d[4] & 0x20 ? "LOWBAT" : "_",
	  d[4] & 4 ? "Hz" : "_", d[4] & 1 ? "HOLD" : "SAMPLE",
	  d[4] & 8 ? "OVERFLOW" : "_", d[4] & 0x10 ? "-" : "+");

}

void
printxvalue (unsigned char *d, int len, int dot)
{

  int i;
  int r;
//  printf("<%d %d > ",base_range,sub_range);
  //     if(sub_range==6 && d[1]==0xE0)
  //      sub_range+=2;

//  printf("<%d %d> ",base_range[sub_range],sub_range);
  d += 5;
  r = base_range[sub_range] + 1;
  for (i = 0; i < 5; i++, d++)
    {
      if (*d == 0x3f)
	{
	  putchar (' ');
	  continue;
	}
      if (*d == 0x3e)
	{
	  putchar ('L');
	  continue;
	}
      if (*d >= 0x30 && *d < 0x3a)
	{
	  if (r == 1)
	    printf (".");
	  r--;
	  putchar (*d);
	  continue;
	}
      {
	fprintf (stderr, "DEBUG, unknown character in ascci adc value %02X: ",
		 *d);
	dumpdata (d, len, stderr);
      }
    }
}

void
printmode (unsigned char *buf, int len)
{

  switch (buf[1])
    {				//measurment mode
    case (0xF8):
      printf ("AC V ");
      base_range = F8_range;
      break;
    case (0xF0):
      printf ("DC V ");
      base_range = F0_range;
      break;
    case (0xE8):
      printf ("DC mV ");
      base_range = E8_range;
      break;
    case (0xE0):
      printf ("R ");
      base_range = E0_range;	//800
      break;
    case (0xE1):
      printf ("C ");
      base_range = E1_range;	//8
      break;
    case (0xD8):
      printf ("D ");
      base_range = D8_range;	//8
      break;
    case (0xA8):
      printf ("DC A ");
      base_range = A8_range;	//8
      break;
    case (0xA9):
      printf ("AC A ");
      base_range = A9_range;	//8
      break;
    case (0xB0):
      printf ("AC mA ");
      base_range = B0_range;	//80
      break;
    case (0xB1):
      printf ("DC mA ");
      base_range = B1_range;	//80
      break;
    default:
      printf ("?");
      base_range = FAIL_range;
      fprintf (stderr, "DEBUG, unknown mode %02X: ", buf[1]);
      dumpdata (buf, len, stderr);
    }
}

int
initserial (char *name)
{
  int fd, line;
  /* Open the device device and lock it. */
  if ((fd = open (name, O_RDWR | O_NONBLOCK)) == -1)
    exit (2);
  if (flock (fd, LOCK_EX | LOCK_NB) == -1)
    exit (3);
  /* Flush input and output queues. */
  if (ioctl (fd, TCFLSH, 2) != 0)
    exit (4);
  /* Fetch the current terminal parameters. */
  if (ioctl (fd, TCGETS, &ts) != 0)
    exit (4);
  ts.c_iflag = 0;
  ts.c_lflag = 0;
  ts.c_oflag = 0;
  ts.c_cc[VINTR] = '\0';
  ts.c_cc[VQUIT] = '\0';
  ts.c_cc[VERASE] = '\0';
  ts.c_cc[VKILL] = '\0';
  ts.c_cc[VEOF] = '\0';
  ts.c_cc[VTIME] = '\0';
  ts.c_cc[VMIN] = 1;
  ts.c_cc[VSWTC] = '\0';
  ts.c_cc[VSTART] = '\0';
  ts.c_cc[VSTOP] = '\0';
  ts.c_cc[VSUSP] = '\0';
  ts.c_cc[VEOL] = '\0';
  ts.c_cc[VREPRINT] = '\0';
  ts.c_cc[VDISCARD] = '\0';
  ts.c_cc[VWERASE] = '\0';
  ts.c_cc[VLNEXT] = '\0';
  ts.c_cc[VEOL2] = '\0';
  /* Sets hardware control flags:                              */
  /* 9600 baud = default                                       */
  /* 8 data bits                                               */
  /* Enable receiver                                           */
  /* Normal use RTS/CTS flow control                           */
  ts.c_cflag = CS8 | CREAD | B9600;
  /* Sets the new terminal parameters. */
  if (ioctl (fd, TCSETS, &ts) != 0)
    exit (4);
  /* Turn off RTS control line. */
  line = TIOCM_RTS;
  if (ioctl (fd, TIOCMBIC, &line) != 0)
    exit (4);
  /* Turn on DTR control line. */
  line = TIOCM_DTR;
  if (ioctl (fd, TIOCMBIS, &line) != 0)
    exit (4);
  return (fd);
}

void
restoreserial (int fd)
{
  if (flock (fd, LOCK_UN) == -1)
    exit (3);
  if (close (fd) != 0)
    exit (2);
}
