/*
 * --------------
 *  VHD Sherlock
 * --------------
 *  sherlock.c
 * -----------
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Read the README file for the changelog and information on how to
 * compile and use this program.
 */

// Global definitions (don't mess with those)

#define	_GNU_SOURCE
//#define _LARGEFILE64_SOURCE
#define	_FILE_OFFSET_BITS	64
#define MT_PROGNAME		"VHD Sherlock"
#define MT_PROGNAME_LEN		strlen(MT_PROGNAME)

// Header files
#include <stdio.h>			// *printf
#include <stdlib.h>			// malloc, free
#include <string.h>			// strlen, memset, strncmp, memcpy
#include <unistd.h>			// getopt, close, read, lseek
#include <inttypes.h>			// PRI*
#include <endian.h>			// be*toh
#include <sys/types.h>			// *int*_t, open, lseek
#include <sys/stat.h>			// open
#include <fcntl.h>			// open

// Global definitions
#define	MT_CKS		8		// Size of "cookie" entries in headers
#define	MT_SECS		512		// Size of a sector

// VHD Footer structure
typedef struct {
	u_char		cookie[MT_CKS];	// Cookie
	u_int32_t	features;	// Features
	u_int32_t	ffversion;	// File format version
	u_int64_t	dataoffset;	// Data offset	
	u_int32_t	timestamp;	// Timestamp
	u_int32_t	creatorapp;	// Creator application
	u_int32_t	creatorver;	// Creator version
	u_int32_t	creatorhos;	// Creator host OS
	u_int64_t	origsize;	// Original size
	u_int64_t	currsize;	// Current size
	u_int32_t	diskgeom;	// Disk geometry
	u_int32_t	disktype;	// Disk type
	u_int32_t	checksum;	// Checksum
	u_char		uniqueid[16];	// Unique ID
	u_char		savedst;	// Saved state
	u_char		reserved[427];	// Reserved
}__attribute__((__packed__)) vhd_footer_t;

// VHD Dynamic Disk Header structure
typedef struct {
	u_char		cookie[MT_CKS];	// Cookie
	u_int64_t	dataoffset;	// Data offset
	u_int64_t	tableoffset;	// Table offset
	u_int32_t	headerversion;	// Header version
	u_int32_t	maxtabentries;	// Max table entries
	u_int32_t	blocksize;	// Block size
	u_int32_t	checksum;	// Checksum
	u_char		parentuuid[16];	// Parent Unique ID
	u_int32_t	parentts;	// Parent Timestamp
	u_char		reserved1[4];	// Reserved
	u_char		parentname[512];// Parent Unicode Name
	u_char		parentloc1[24];	// Parent Locator Entry 1
	u_char		parentloc2[24];	// Parent Locator Entry 2
	u_char		parentloc3[24];	// Parent Locator Entry 3
	u_char		parentloc4[24];	// Parent Locator Entry 4
	u_char		parentloc5[24];	// Parent Locator Entry 5
	u_char		parentloc6[24];	// Parent Locator Entry 6
	u_char		parentloc7[24];	// Parent Locator Entry 7
	u_char		parentloc8[24];	// Parent Locator Entry 8
	u_char		reserved2[256];	// Reserved
}__attribute__((__packed__)) vhd_ddhdr_t;

// Auxiliary functions

// Print help
void	usage(char *progname){
	// Local variables
	int	j;				// Temporary integer

	// Print help
	for (j=0; j<MT_PROGNAME_LEN; j++) printf("-");
	printf("\n%s\n", MT_PROGNAME);
	for (j=0; j<MT_PROGNAME_LEN; j++) printf("-");
	printf("\nUsage: %s [ -h ] [ -v[v] ] <file>\n", progname);
	printf("       -h		Print this help message and quit.\n");
	printf("       -v		Increase verbose level (may be used multiple times).\n");
	printf("       -c		Read VHD footer *copy* only (for corrupted VHDs with no footer)\n");
	printf("       <file>		VHD file to examine\n");
}

// Convert a 16 bit uuid to a static string
char *	uuidstr(u_char uuid[16]){
	// Local variables
	static u_char	str[37];		// String representation of UUID
	char		*ptr;			// Temporary pointer
	int		i;			// Temporary integer

	// Fill str
	ptr = (char *)&str;
	for (i=0; i<16; i++){
		sprintf(ptr, "%02x", uuid[i]);
		ptr+=2;
		if ((i==3) || (i==5) || (i==7) || (i==9)){
			sprintf(ptr++, "-");
		}
	}
	*ptr=0;

	// Return a pointer to the static area
	return((char *)&str);
}

// Extract cylinder from the 4 byte disk geometry field
inline u_int16_t dg2cyli(u_int32_t diskgeom){
	return((u_int16_t)((be32toh(diskgeom)&0xFFFF0000)>>16));
}

// Extract heads from the 4 byte disk geometry field
inline u_int8_t	dg2head(u_int32_t diskgeom){
	return((u_int8_t)((be32toh(diskgeom)&0x0000FF00)>>8));
}

// Extract sectors per track/cylinder from the 4 byte disk geometry field
inline u_int8_t	dg2sptc(u_int32_t diskgeom){
	return((u_int8_t)((be32toh(diskgeom)&0x000000FF)));
}

// Convert a disk size to a human readable static string
char *	size2h(u_int64_t disksize){
	// Local variables
	static char	str[32];
	u_int16_t	div = 0;
	u_int64_t	rem = 0;

	// Correct endianess
	disksize = be64toh(disksize);

	// Loop dividing disksize
	while (((disksize / 1024) > 0)&&(div<4)){
		div++;
		rem = disksize % 1024;
		disksize /= 1024;
		if (rem){
			break;
		}
	}

	// Find out unit and fill str accordingly
	switch (div){
		case 0:
			snprintf(str, sizeof(str), "%"PRIu64" B", disksize);
			break;
		case 1:
			if (rem){
				snprintf(str, sizeof(str), "%"PRIu64" KiB + %"PRIu64" B", disksize, rem);
			}else{
				snprintf(str, sizeof(str), "%"PRIu64" KiB", disksize);
			}
			break;
		case 2:
			if (rem){
				snprintf(str, sizeof(str), "%"PRIu64" MiB + %"PRIu64" KiB", disksize, rem);
			}else{
				snprintf(str, sizeof(str), "%"PRIu64" MiB", disksize);
			}
			break;
		case 3:
			if (rem){
				snprintf(str, sizeof(str), "%"PRIu64" GiB + %"PRIu64" MiB", disksize, rem);
			}else{
				snprintf(str, sizeof(str), "%"PRIu64" GiB", disksize);
			}
			break;
		default:
			if (rem){
				snprintf(str, sizeof(str), "%"PRIu64" TiB + %"PRIu64" GiB", disksize, rem);
			}else{
				snprintf(str, sizeof(str), "%"PRIu64" TiB", disksize);
			}
			break;
	}

	// Return a poniter to the static area
	return((char *)&str);
}

// Convert a disk type to a readable static string
char *	dt2str(u_int32_t disktype){
	// Local variables
	static char	str[32];

	// Convert according to known disk types
	switch (be32toh(disktype)){
		case 0:
			snprintf(str, sizeof(str), "None");
			break;
		case 2:
			snprintf(str, sizeof(str), "Fixed hard disk");
			break;
		case 3:
			snprintf(str, sizeof(str), "Dynamic hard disk");
			break;
		case 4:
			snprintf(str, sizeof(str), "Differencing hard disk");
			break;
		case 1:
		case 5:
		case 6:
			snprintf(str, sizeof(str), "Reserved (deprecated)");
			break;
		default:
			snprintf(str, sizeof(str), "Unknown disk type");
	}

	// Return a pointer to the static area
	return((char *)&str);
}

void	dump_vhdfooter(vhd_footer_t *foot){
	// Local variables
	char		cookie_str[MT_CKS+1];	// Temporary buffer

	// Print a footer
	printf("------------------------\n");
	printf(" VHD Footer (%d bytes)\n", sizeof(vhd_footer_t));
	printf("------------------------\n");
	snprintf(cookie_str, sizeof(cookie_str), "%s", foot->cookie);
	printf(" Cookie              = %s\n",             cookie_str);
	printf(" Features            = 0x%08X\n",         be32toh(foot->features));
	printf(" File Format Version = 0x%08X\n",         be32toh(foot->ffversion));
	printf(" Data Offset         = 0x%016"PRIX64"\n", be64toh(foot->dataoffset));
	printf(" Time Stamp          = 0x%08X\n",         be32toh(foot->timestamp));
	printf(" Creator Application = 0x%08X\n",         be32toh(foot->creatorapp));
	printf(" Creator Version     = 0x%08X\n",         be32toh(foot->creatorver));
	printf(" Creator Host OS     = 0x%08X\n",         be32toh(foot->creatorhos));
	printf(" Original Size       = 0x%016"PRIX64"\n", be64toh(foot->origsize));
	printf("                     = %s\n",             size2h(foot->origsize));
	printf(" Current Size        = 0x%016"PRIX64"\n", be64toh(foot->currsize));
	printf("                     = %s\n",             size2h(foot->currsize));
	printf(" Disk Geometry       = 0x%08X\n",         be32toh(foot->diskgeom));
	printf("           Cylinders = %hu\n",            dg2cyli(foot->diskgeom));
	printf("               Heads = %hhu\n",           dg2head(foot->diskgeom));
	printf("       Sectors/Track = %hhu\n",           dg2sptc(foot->diskgeom));
	printf(" Disk Type           = 0x%08X\n",         be32toh(foot->disktype));
	printf("                     = %s\n",             dt2str(foot->disktype));
	printf(" Checksum            = 0x%08X\n",         be32toh(foot->checksum));
	printf(" Unique ID           = %s\n",             uuidstr(foot->uniqueid));
	printf(" Saved State         = 0x%02X\n",         foot->savedst);
	printf(" Reserved            = <...427 bytes...>\n");
	printf("===============================================\n");
}

void	dump_vhd_dyndiskhdr(vhd_ddhdr_t *ddhdr){
	// Local variables
	char		cookie_str[MT_CKS+1];	// Temporary buffer

	// Print a footer
	printf("--------------------------------------\n");
	printf(" VHD Dynamic Disk Header (%d bytes)\n", sizeof(vhd_ddhdr_t));
	printf("--------------------------------------\n");
	snprintf(cookie_str, sizeof(cookie_str), "%s", ddhdr->cookie);
	printf(" Cookie              = %s\n",             cookie_str);
	printf(" Data Offset         = 0x%016"PRIX64"\n", be64toh(ddhdr->dataoffset));
	printf(" Table Offset        = 0x%016"PRIX64"\n", be64toh(ddhdr->tableoffset));
	printf(" Header Version      = 0x%08X\n",         be32toh(ddhdr->headerversion));
	printf(" Max Table Entries   = 0x%08X\n",         be32toh(ddhdr->maxtabentries));
	printf(" Block Size          = 0x%08X\n",         be32toh(ddhdr->blocksize));
	printf(" Checksum            = 0x%08X\n",         be32toh(ddhdr->checksum));
	printf(" Parent UUID         = %s\n",             uuidstr(ddhdr->parentuuid));
	printf(" Parent TS           = 0x%08X\n",         be32toh(ddhdr->parentts));
	printf("                       %u (10)\n",        be32toh(ddhdr->parentts));
	printf(" Reserved            = <...4 bytes...>\n");
	printf(" Parent Unicode Name = <...512 bytes...>\n");
	printf(" Parent Loc Entry 1  = <...24 bytes...>\n");
	printf(" Parent Loc Entry 2  = <...24 bytes...>\n");
	printf(" Parent Loc Entry 3  = <...24 bytes...>\n");
	printf(" Parent Loc Entry 4  = <...24 bytes...>\n");
	printf(" Parent Loc Entry 5  = <...24 bytes...>\n");
	printf(" Parent Loc Entry 6  = <...24 bytes...>\n");
	printf(" Parent Loc Entry 7  = <...24 bytes...>\n");
	printf(" Parent Loc Entry 8  = <...24 bytes...>\n");
	printf("===============================================\n");
}


// Main function
int main(int argc, char **argv){
	// Local variables

	// VHD File specific
	int		vhdfd;			// VHD file descriptor
	vhd_footer_t	vhd_footer_copy;	// VHD footer copy (beginning of file)
	vhd_ddhdr_t	vhd_dyndiskhdr;		// VHD Dynamic Disk Header
	u_int32_t	*batmap;		// Block allocation table map
	char		secbitmap[MT_SECS];	// Sector bitmap temporary buffer
	vhd_footer_t	vhd_footer;		// VHD footer (end of file)
	char		copyonly = 0;

	// General
	int		verbose = 0;		// Verbose level
	int		i, j;			// Temporary integers
	ssize_t		bytesread;		// Bytes read in a read operation

	// Fetch arguments
	while ((i = getopt(argc, argv, "hvc")) != -1){
		switch (i){
			case 'h':
				// Print help
				usage(argv[0]);
				return(0);
				break;

			case 'v':
				// Increase verbose level
				verbose++;
				break;

			case 'c':
				// Read VHD footer copy only
				if (copyonly){
					fprintf(stderr, "Error! -c can only be used once.\n");
					usage(argv[0]);
					return(1);
				}
				copyonly = 1;
				break;
		}
	}

	// Validate there is a filename
	if (argc != optind+1) {
		// Print help
		usage(argv[0]);
		return(0);
	}

	// Initialise local variables
	memset(&vhd_footer_copy, 0, sizeof(vhd_footer_copy));
	memset(&vhd_footer, 0, sizeof(vhd_footer));

	// Open VHD file
	if (verbose){
		printf("Opening VHD file...\n");
	}
	if ((vhdfd = open(argv[optind], O_RDONLY | O_LARGEFILE)) < 0){
		perror("open");
		fprintf(stderr, "%s: Error opening VHD file \"%s\".\n", argv[0], argv[optind]);
		return(1);
	}
	if (verbose){
		printf("...ok\n\n");
	}

	// Read the VHD footer
	if (copyonly == 1){
		if (verbose){
			printf("Reading VHD footer copy exclusively...\n");
		}
		bytesread = read(vhdfd, &vhd_footer_copy, sizeof(vhd_footer_copy));
		if (bytesread != sizeof(vhd_footer_copy)){
			fprintf(stderr, "Corrupt disk detected whilst reading VHD footer copy.\n");
			fprintf(stderr, "Expecting %d bytes. Read %d bytes.\n", sizeof(vhd_footer_copy), bytesread);
			close(vhdfd);
			return(1);
		}
		if (strncmp((char *)&(vhd_footer_copy.cookie), "conectix", 8)){
			fprintf(stderr, "Corrupt disk detect whilst reading VHD footer copy.\n");
			fprintf(stderr, "Expected cookie (\"conectix\") missing or corrupt.\n");
			close(vhdfd);
			return(1);
		}
		memcpy(&vhd_footer, &vhd_footer_copy, sizeof(vhd_footer));
		if (verbose){
			printf("...ok\n\n");
		}
	}else{
		if (verbose){
			printf("Positioning descriptor to VHD footer...\n");
		}

		// little hack to get around a SEEK_SET problem
		bytesread = lseek(vhdfd, -sizeof(vhd_footer), SEEK_END);
		if (lseek(vhdfd, bytesread, SEEK_SET) < 0){
			perror("lseek");
			fprintf(stderr, "Corrupt disk detected whilst reading VHD footer.\n");
			fprintf(stderr, "Error repositioning VHD descriptor to the footer.\n");
			close(vhdfd);
			return(1);
		}
		if (verbose){
			printf("...ok\n\n");
			printf("Reading VHD footer...\n");
		}
		bytesread = read(vhdfd, &vhd_footer, sizeof(vhd_footer));
		if (bytesread != sizeof(vhd_footer)){
			fprintf(stderr, "Corrupt disk detected whilst reading VHD footer.\n");
			fprintf(stderr, "Expecting %d bytes. Read %d bytes.\n", sizeof(vhd_footer), bytesread);
			close(vhdfd);
			return(1);
		}
		if (strncmp((char *)&(vhd_footer.cookie), "conectix", 8)){
			fprintf(stderr, "Corrupt disk detected after reading VHD footer.\n");
			fprintf(stderr, "Expected cookie (\"conectix\") missing or corrupt.\n");
			close(vhdfd);
			return(1);
		}
		if (verbose){
			printf("...ok\n\n");
		}

		// Dump footer
		if (verbose > 1){
			dump_vhdfooter(&vhd_footer);
		}
	}

	// Check type of disk
	if (verbose){
		printf("Detecting disk type...\n");
	}
	switch(be32toh(vhd_footer.disktype)){
		case 2:
			if (verbose){
				printf("===> Fixed hard disk detected.\n...ok\n\n");
			}
			break;
		case 3:
			if (verbose){
				printf("===> Dynamic hard disk detected.\n...ok\n\n");
			}
			goto dyndisk;
			break;
		case 4:
			if (verbose){
				printf("===> Differencing hard disk detected.\n...ok\n\n");
			}
			goto dyndisk;
			break;
		default:
			printf("===> Unknown VHD disk type: %d\n", be32toh(vhd_footer.disktype));
			break;
	}
	goto out;

dyndisk:
	// Read the VHD footer copy
	if (verbose){
		printf("Positioning descriptor to read VHD footer copy...\n");
	}
	if (lseek(vhdfd, 0, SEEK_SET) < 0){
		perror("lseek");
		fprintf(stderr, "Error repositioning VHD descriptor to the file start.\n");
		close(vhdfd);
		return(1);
	}
	if (verbose){
		printf("...ok\n\n");
		printf("Reading VHD footer copy...\n");
	}
	bytesread = read(vhdfd, &vhd_footer_copy, sizeof(vhd_footer_copy));
	if (bytesread != sizeof(vhd_footer_copy)){
		fprintf(stderr, "Corrupt disk detected whilst reading VHD footer copy.\n");
		fprintf(stderr, "Expecting %d bytes. Read %d bytes.\n", sizeof(vhd_footer_copy), bytesread);
		close(vhdfd);
		return(1);
	}
	if (strncmp((char *)&(vhd_footer_copy.cookie), "conectix", 8)){
		fprintf(stderr, "Corrupt disk detect whilst reading VHD footer copy.\n");
		fprintf(stderr, "Expected cookie (\"conectix\") missing or corrupt.\n");
		close(vhdfd);
		return(1);
	}
	if (verbose){
		printf("...ok\n\n");
	}

	// Dump footer copy
	if (verbose > 1){
		dump_vhdfooter(&vhd_footer);
	}

	// Read the VHD dynamic disk header
	if (verbose){
		printf("Reading VHD dynamic disk header...\n");
	}
	bytesread = read(vhdfd, &vhd_dyndiskhdr, sizeof(vhd_dyndiskhdr));
	if (bytesread != sizeof(vhd_dyndiskhdr)){
		fprintf(stderr, "Corrupt disk detected whilst reading VHD Dynamic Disk Header.\n");
		fprintf(stderr, "Expecting %d bytes. Read %d bytes.\n", sizeof(vhd_dyndiskhdr), bytesread);
		close(vhdfd);
		return(1);
	}
	if (strncmp((char *)&(vhd_dyndiskhdr.cookie), "cxsparse", 8)){
		fprintf(stderr, "Corrupt disk detect whilst reading Dynamic Disk Header.\n");
		fprintf(stderr, "Expected cookie (\"cxsparse\") missing or corrupt.\n");
		close(vhdfd);
		return(1);
	}
	if (verbose){
		printf("...ok\n\n");
	}

	// Dump VHD dynamic disk header
	if (verbose > 1){
		dump_vhd_dyndiskhdr(&vhd_dyndiskhdr);
	}

	// Allocate Batmap
	if (verbose){
		printf("Allocating batmap...\n");
	}
	if ((batmap = (u_int32_t *)malloc(sizeof(u_int32_t)*be32toh(vhd_dyndiskhdr.maxtabentries))) == NULL){
		perror("malloc");
		fprintf(stderr, "Error allocating %u bytes for the batmap.\n", be32toh(vhd_dyndiskhdr.maxtabentries));
		close(vhdfd);
		return(1);
	}
	if (verbose){
		printf("...ok\n\n");
	}

	// Read batmap
	if (verbose){
		printf("Positioning descriptor to read VHD batmap...\n");
	}
	if (lseek(vhdfd, be64toh(vhd_dyndiskhdr.tableoffset), SEEK_SET) < 0){
		perror("lseek");
		fprintf(stderr, "Error repositioning VHD descriptor to batmap at 0x%016"PRIX64"\n", be64toh(vhd_footer_copy.dataoffset));
		free(batmap);
		close(vhdfd);
		return(1);
	}
	if (verbose){
		printf("...ok\n\n");
		printf("Reading VHD batmap...\n");
	}
	bytesread = read(vhdfd, batmap, sizeof(u_int32_t)*be32toh(vhd_dyndiskhdr.maxtabentries));
	if (bytesread != sizeof(u_int32_t)*be32toh(vhd_dyndiskhdr.maxtabentries)){
		fprintf(stderr, "Error reading batmap.\n");
		free(batmap);
		close(vhdfd);
		return(1);
	}
	if (verbose){
		printf("...ok\n");
	}

	// Dump Batmap
	if (verbose > 2){
		printf("----------------------------\n");
		printf(" VHD Block Allocation Table (%u entries)\n", be32toh(vhd_dyndiskhdr.maxtabentries));
		printf("----------------------------\n");
		for (i=0; i<be32toh(vhd_dyndiskhdr.maxtabentries); i++){
			printf("batmap[%d] = 0x%08X\n", i, be32toh(batmap[i]));
		}
		printf("===============================================\n");
	}

	// Dump sector bitmaps
	if (verbose > 2){
		printf("------------------------------\n");
		printf(" VHD Sector Bitmaps per Block\n");
		printf("------------------------------\n");
		for (i=0; i<be32toh(vhd_dyndiskhdr.maxtabentries); i++){
			if (batmap[i] == 0xFFFFFFFF){
				printf(" block[%d] = <...not allocated...>\n", i);
				continue;
			}
			if (lseek(vhdfd, be32toh(batmap[i])*MT_SECS, SEEK_SET) < 0){
				perror("lseek");
				fprintf(stderr, "Error repositioning VHD descriptor to batmap[%d] at 0x%016X\n", i, be32toh(batmap[i]));
				free(batmap);
				close(vhdfd);
				return(1);
			}
			bytesread = read(vhdfd, &secbitmap, MT_SECS);
			if (bytesread != MT_SECS){
				fprintf(stderr, "Error reading sector bitmap (batmap[%d] at 0x%016X.\n", i, be32toh(batmap[i]));
				free(batmap);
				close(vhdfd);
				return(1);
			}

			printf(" block[%d] sector bitmap =", i);
			for (j=0; j<MT_SECS; j++){
				if (!(j%32)){
					printf("\n ");
				}
				printf("%02hhX", secbitmap[j]);
			}
			printf("\n");
		}
		
	}

	// Free batmap
	free(batmap);

	// Print summary
	//printf("VHD is OK\n");

out:
	// Close file descriptor and return success
	close(vhdfd);
	return(0);
}
