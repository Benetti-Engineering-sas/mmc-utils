/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 *
 * Modified to add field firmware update support,
 * those modifications are Copyright (c) 2016 SanDisk Corp.
 *
 * (This code is based on btrfs-progs/btrfs.c.)
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmc_cmds.h"

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct Command {
	CommandFunction	func;	/* function which implements the command */
	int	nargs;		/* if == 999, any number of arguments
				   if >= 0, number of arguments,
				   if < 0, _minimum_ number of arguments */
	char	*verb;		/* verb */
	char	*help;		/* help lines; from the 2nd line onward they
                                   are automatically indented */

	/* the following fields are run-time filled by the program */
	char	**cmds;		/* array of subcommands */
	int	ncmds;		/* number of subcommand */
};

static struct Command commands[] = {
	/*
	 *	avoid short commands different for the case only
	 */
	{ do_read_extcsd, -1,
	  "extcsd read", "<device>\n"
		"Print extcsd data from <device>.",
	},
	{ do_write_extcsd, 3,
	  "extcsd write", "<offset> <value> <device>\n"
		  "Write <value> at offset <offset> to <device>'s extcsd.",
	},
	{ do_writeprotect_boot_get, -1,
	  "writeprotect boot get", "<device>\n"
		"Print the boot partitions write protect status for <device>.",
	},
	{ do_writeprotect_boot_set, -1,
	  "writeprotect boot set",
#ifdef DANGEROUS_COMMANDS_ENABLED
		"[-p] "
#endif /* DANGEROUS_COMMANDS_ENABLED */
		"<device> [<number>]\n"
		"Set the boot partition write protect status for <device>.\n"
		"If <number> is passed (0 or 1), only protect that particular\n"
		"eMMC boot partition, otherwise protect both. It will be\n"
		"write-protected until the next boot.\n"
#ifdef DANGEROUS_COMMANDS_ENABLED
		"  -p  Protect partition permanently instead.\n"
		"      NOTE! -p is a one-time programmable (unreversible) change.\n"
#endif /* DANGEROUS_COMMANDS_ENABLED */
	  ,
	},
	{ do_writeprotect_user_set, -4,
	  "writeprotect user set", "<type>" "<start block>" "<blocks>" "<device>\n"
#ifdef DANGEROUS_COMMANDS_ENABLED
	  "Set the write protect configuration for the specified region\nof the user area for <device>.\n<type> must be \"none|temp|pwron|perm\".\n    \"none\"  - Clear temporary write protection.\n    \"temp\"  - Set temporary write protection.\n    \"pwron\" - Set write protection until the next poweron.\n    \"perm\"  - Set permanent write protection.\n<start block> specifies the first block of the protected area.\n<blocks> specifies the size of the protected area in blocks.\nNOTE! The area must start and end on Write Protect Group\nboundries, Use the \"writeprotect user get\" command to get the\nWrite Protect Group size.\nNOTE! \"perm\" is a one-time programmable (unreversible) change.",
#else
	  "Set the write protect configuration for the specified region\nof the user area for <device>.\n<type> must be \"none|temp|pwron\".\n    \"none\"  - Clear temporary write protection.\n    \"temp\"  - Set temporary write protection.\n    \"pwron\" - Set write protection until the next poweron.\n<start block> specifies the first block of the protected area.\n<blocks> specifies the size of the protected area in blocks.\nNOTE! The area must start and end on Write Protect Group\nboundries, Use the \"writeprotect user get\" command to get the\nWrite Protect Group size.",
#endif /* DANGEROUS_COMMANDS_ENABLED */
	},
	{ do_writeprotect_user_get, -1,
	  "writeprotect user get", "<device>\n"
		"Print the user areas write protect configuration for <device>.",
	},
	{ do_disable_512B_emulation, -1,
	  "disable 512B emulation", "<device>\n"
		"Set the eMMC data sector size to 4KB by disabling emulation on\n<device>.",
	},
	{ do_create_gp_partition, -6,
	  "gp create", "<-y|-n|-c> " "<length KiB> " "<partition> " "<enh_attr> " "<ext_attr> " "<device>\n"
		"Create general purpose partition for the <device>.\nDry-run only unless -y or -c is passed.\nUse -c if more partitioning settings are still to come.\nNOTE!  This is a one-time programmable (unreversible) change.\nTo set enhanced attribute to general partition being created set\n <enh_attr> to 1 else set it to 0.\nTo set extended attribute to general partition\n set <ext_attr> to 1,2 else set it to 0",
	},
	{ do_enh_area_set, -4,
	  "enh_area set", "<-y|-n|-c> " "<start KiB> " "<length KiB> " "<device>\n"
		"Enable the enhanced user area for the <device>.\nDry-run only unless -y or -c is passed.\nUse -c if more partitioning settings are still to come.\nNOTE!  This is a one-time programmable (unreversible) change.",
	},
	{ do_write_reliability_set, -2,
	  "write_reliability set", "<-y|-n|-c> " "<partition> " "<device>\n"
		"Enable write reliability per partition for the <device>.\nDry-run only unless -y or -c is passed.\nUse -c if more partitioning settings are still to come.\nNOTE!  This is a one-time programmable (unreversible) change.",
	},
	{ do_status_get, -1,
	  "status get", "<device>\n"
	  "Print the response to STATUS_SEND (CMD13).",
	},
	{ do_write_boot_en, -3,
	  "bootpart enable", "<boot_partition> " "<send_ack> " "<device>\n"
		"Enable the boot partition for the <device>.\nDisable the boot partition for the <device> if <boot_partition> is set to 0.\nTo receive acknowledgment of boot from the card set <send_ack>\nto 1, else set it to 0.",
	},
	{ do_boot_bus_conditions_set, -4,
	  "bootbus set", "<boot_mode> " "<reset_boot_bus_conditions> " "<boot_bus_width> " "<device>\n"
	  "Set Boot Bus Conditions.\n"
	  "<boot_mode> must be \"single_backward|single_hs|dual\"\n"
	  "<reset_boot_bus_conditions> must be \"x1|retain\"\n"
	  "<boot_bus_width> must be \"x1|x4|x8\"",
	},
	{ do_write_bkops_en, -2,
	  "bkops_en", "<auto|manual> <device>\n"
		"Enable the eMMC BKOPS feature on <device>.\n"
		"The auto (AUTO_EN) setting is only supported on eMMC 5.0 or newer.\n"
		"Setting auto won't have any effect if manual is set.\n"
		"NOTE!  Setting manual (MANUAL_EN) is one-time programmable (unreversible) change.",
	},
	{ do_hwreset_en, -1,
	  "hwreset enable", "<device>\n"
		"Permanently enable the eMMC H/W Reset feature on <device>.\nNOTE!  This is a one-time programmable (unreversible) change.",
	},
	{ do_hwreset_dis, -1,
	  "hwreset disable", "<device>\n"
		"Permanently disable the eMMC H/W Reset feature on <device>.\nNOTE!  This is a one-time programmable (unreversible) change.",
	},
	{ do_sanitize, -1,
	  "sanitize", "<device> [timeout_ms]\n"
		"Send Sanitize command to the <device>.\nThis will delete the unmapped memory region of the device.",
	},
	{ do_rpmb_write_key, 2,
	  "rpmb write-key", "<rpmb device> <key file>\n"
		  "Program authentication key which is 32 bytes length and stored\n"
		  "in the specified file. Also you can specify '-' instead of\n"
		  "key file path to read the key from stdin.\n"
		  "NOTE!  This is a one-time programmable (unreversible) change.\n"
		  "Example:\n"
		  "  $ echo -n AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH | \\\n"
		  "    mmc rpmb write-key /dev/mmcblk0rpmb -",
	},
	{ do_rpmb_read_counter, 1,
	  "rpmb read-counter", "<rpmb device>\n"
		  "Counter value for the <rpmb device> will be read to stdout.",
	},
	{ do_rpmb_read_block, -4,
	  "rpmb read-block", "<rpmb device> <address> <blocks count> <output file> [key file]\n"
		  "Blocks of 256 bytes will be read from <rpmb device> to output\n"
		  "file or stdout if '-' is specified. If key is specified - read\n"
		  "data will be verified. Instead of regular path you can specify\n"
		  "'-' to read key from stdin.\n"
		  "Example:\n"
		  "  $ echo -n AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH | \\\n"
		  "    mmc rpmb read-block /dev/mmcblk0rpmb 0x02 2 /tmp/block -\n"
		  "or read two blocks without verification\n"
		  "  $ mmc rpmb read-block /dev/mmcblk0rpmb 0x02 2 /tmp/block",
	},
	{ do_rpmb_write_block, 4,
	  "rpmb write-block", "<rpmb device> <address> <256 byte data file> <key file>\n"
		  "Block of 256 bytes will be written from data file to\n"
		  "<rpmb device>. Also you can specify '-' instead of key\n"
		  "file path or data file to read the data from stdin.\n"
		  "Example:\n"
		  "  $ (awk 'BEGIN {while (c++<256) printf \"a\"}' | \\\n"
		  "    echo -n AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH) | \\\n"
		  "    mmc rpmb write-block /dev/mmcblk0rpmb 0x02 - -",
	},
	{ do_rpmb_sec_wp_enable, 3,
	  "rpmb secure-wp-mode-on", "<dev> <rpmb device> <key file>\n"
		  "Enable Secure Write Protection mode.\n"
		  "The access to the write protection related EXT_CSD and\n"
		  "CSD fields depends on the value of SECURE_WP_MASK bit in\n"
		  "SECURE_WP_MODE_CONFIG field\n"
		  "You can specify '-' instead of key\n"
		  "Example:\n"
		  "    echo -n AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH | \\\n"
		  "    mmc rpmb secure-wp-mode-on /dev/block/mmcblk0 /dev/mmcblk0rpmb -",
	},
	{ do_rpmb_sec_wp_disable, 3,
	  "rpmb secure-wp-mode-off", "<dev> <rpmb device> <key file>\n"
		  "Legacy Write Protection mode.\n"
		  "TMP_WRITE_PROTECT[12] and PERM_WRITE_PROTECT[13] is updated by CMD27.\n"
		  "USER_WP[171], BOOT_WP[173] and BOOT_WP_STATUS[174] are updated by CMD6\n"
		  "You can specify '-' instead of key\n"
		  "Example:\n"
		  "    echo -n AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH | \\\n"
		  "    mmc rpmb secure-wp-mode-off /dev/block/mmcblk0 /dev/mmcblk0rpmb -",
	},
	{ do_rpmb_sec_wp_mode_set, 3,
	  "rpmb secure-wp-disable", "<dev> <rpmb device> <key file>\n"
		  "Enabling updating WP related EXT_CSD and CSD fields.\n"
		  "Applicable only if secure wp mode is enabled.\n"
		  "You can specify '-' instead of key\n"
		  "Example:\n"
		  "    echo -n AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH | \\\n"
		  "    mmc rpmb secure-wp-disable /dev/block/mmcblk0 /dev/mmcblk0rpmb -",
	},
	{ do_rpmb_sec_wp_mode_clear, 3,
	  "rpmb secure-wp-enable", "<dev> <rpmb device> <key file>\n"
		  "Disabling updating WP related EXT_CSD and CSD fields.\n"
		  "Applicable only if secure wp mode is enabled.\n"
		  "You can specify '-' instead of key\n"
		  "Example:\n"
		  "    echo -n AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH | \\\n"
		  "    mmc rpmb secure-wp-enable /dev/block/mmcblk0 /dev/mmcblk0rpmb -",
	},
	{ do_rpmb_sec_wp_en_read, -2,
	  "rpmb secure-wp-en-read", "<device> <rpmb device> [key file]\n"
		  "Reads the status of the SECURE_WP_EN & SECURE_WP_MASK fields.\n"
		  "You can specify '-' instead of key\n"
		  "Example:\n"
		  "    echo -n AAAABBBBCCCCDDDDEEEEFFFFGGGGHHHH | \\\n"
		  "    mmc rpmb secure-wp-en-read /dev/mmcblk0 /dev/mmcblk0rpmb -\n"
		  "or read without verification\n"
		  "  $ mmc rpmb secure-wp-en-read /dev/mmcblk0 /dev/mmcblk0rpmb",
	},
	{ do_cache_en, -1,
	  "cache enable", "<device>\n"
		"Enable the eMMC cache feature on <device>.\n"
		"NOTE! The cache is an optional feature on devices >= eMMC4.5.",
	},
	{ do_cache_dis, -1,
	  "cache disable", "<device>\n"
		"Disable the eMMC cache feature on <device>.\n"
		"NOTE! The cache is an optional feature on devices >= eMMC4.5.",
	},
	{ do_cache_flush, 1,
	  "cache flush", "<device>\n"
		"flush the eMMC cache <device>.\n"
		"NOTE! The cache is an optional feature on devices >= eMMC5.0.",
	},
	{ do_read_csd, -1,
	  "csd read", "<device path>\n"
		  "Print CSD data from <device path>.\n"
		  "The device path should specify the csd file directory.",
	},
	{ do_read_cid, -1,
	  "cid read", "<device path>\n"
		  "Print CID data from <device path>.\n"
		  "The device path should specify the cid file directory.",
	},
	{ do_read_scr, -1,
	  "scr read", "<device path>\n"
		  "Print SCR data from <device path>.\n"
		  "The device path should specify the scr file directory.",
	},
	{ do_ffu, -2,
	  "ffu", "<image name> <device> [chunk-bytes]\n"
		"Run Field Firmware Update with <image name> on <device>.\n"
		"[chunk-bytes] is optional and defaults to its max - 512k. "
		"should be in decimal bytes and sector aligned.\n",
	},
	{ do_opt_ffu1, -2,
	 "opt_ffu1", "<image name> <device> [chunk-bytes]\n"
	 "Optional FFU mode 1, it's the same as 'ffu', but uses CMD23+CMD25 for repeated downloads and remains in FFU mode until completion.\n",
	},
	{ do_opt_ffu2, -2,
	 "opt_ffu2", "<image name> <device> [chunk-bytes]\n"
	 "Optional FFU mode 2, uses CMD25+CMD12 Open-ended Multiple-block write to download and remains in FFU mode until completion.\n",
	},
	{ do_opt_ffu3, -2,
	"opt_ffu3", "<image name> <device> [chunk-bytes]\n"
	"Optional FFU mode 3, uses CMD24 Single-block write for downloading, exiting FFU mode after each block written.\n",
	},
	{ do_opt_ffu4, -2,
	 "opt_ffu4", "<image name> <device> [chunk-bytes]\n"
	 "Optional FFU mode 4, uses CMD24 Single-block write for repeated downloads, remaining in FFU mode until completion.\n",
	},
	{ do_erase, -4,
	"erase", "<type> " "<start address> " "<end address> " "<device>\n"
		"Send Erase CMD38 with specific argument to the <device>\n\n"
		"NOTE!: This will delete all user data in the specified region of the device\n"
		"<type> must be: legacy | discard | secure-erase | "
		"secure-trim1 | secure-trim2 | trim \n",
	},
	{ do_general_cmd_read, -1,
	"gen_cmd read", "<device> [arg]\n"
		"Send GEN_CMD (CMD56) to read vendor-specific format/meaning data from <device>\n\n"
		"NOTE!: [arg] is optional and defaults to 0x1. If [arg] is specified, then [arg]\n"
		"must be a 32-bit hexadecimal number, prefixed with 0x/0X. And bit0 in [arg] must\n"
		"be 1.",
	},
	{ do_softreset, -1,
	  "softreset", "<device>\n"
	  "Issues a CMD0 softreset, e.g. for testing if hardware reset for UHS works",
	},
	{ do_preidle, -1,
	  "preidle", "<device>\n"
	  "Issues a CMD0 GO_PRE_IDLE",
	},
	{ do_alt_boot_op, -1,
	  "boot_operation", "<boot_data_file> <device>\n"
	  "Does the alternative boot operation and writes the specified starting blocks of boot data into the requested file.\n\n"
	  "Note some limitations\n:"
	  "1. The boot operation must be configured, e.g. for legacy speed:\n"
	  "mmc-utils bootbus set single_backward retain x8 /dev/mmcblk2\n"
	  "mmc-utils bootpart enable 1 0 /dev/mmcblk2\n"
	  "2. The MMC must currently be running at the bus mode that is configured for the boot operation (HS200 and HS400 not supported at all).\n"
	  "3. Only up to 512K bytes of boot data will be transferred.\n"
	  "4. The MMC will perform a soft reset, if your system cannot handle that do not use the boot operation from mmc-utils.\n",
	},
	{ NULL, 0, NULL, NULL }
};

static char *get_prgname(char *programname)
{
	char	*np;
	np = strrchr(programname,'/');
	if(!np)
		np = programname;
	else
		np++;

	return np;
}

static void print_help(struct Command *cmd)
{
        printf("------------------------------------------------\n");
        printf("Usage for command\t%s %s\n", cmd->verb, cmd->help);
}

static void help(char *np)
{
	struct Command *cp;

	for( cp = commands; cp->verb; cp++ )
		print_help(cp);

	printf("\n\t%s help|--help|-h\n\t\tShow the help.\n", np);
	printf("\n\t%s <cmd> --help\n\t\tShow detailed help for a command or subset of commands.\n", np);
	printf("\n%s\n", VERSION);
}

static int split_command(char *cmd, char ***commands)
{
	int c, l;
	char *p, *s;

	for (*commands = NULL, l = c = 0, p = s = cmd ; ; p++, l++) {
		if ( *p && *p != ' ' )
			continue;

		/* c + 2 so that we have room for the null */
		(*commands) = realloc( (*commands), sizeof(char *)*(c + 2));
		(*commands)[c] = strndup(s, l);
		c++;
		l = 0;
		s = p+1;
		if( !*p ) break;
	}

	(*commands)[c] = NULL;
	return c;
}

/*
	This function checks if the passed command is ambiguous
*/
static int check_ambiguity(struct Command *cmd, char **argv){
	int		i;
	struct Command	*cp;
	/* check for ambiguity */
	for( i = 0 ; i < cmd->ncmds ; i++ ){
		int match;
		for( match = 0, cp = commands; cp->verb; cp++ ){
			int	j, skip;
			char	*s1, *s2;

			if( cp->ncmds < i )
				continue;

			for( skip = 0, j = 0 ; j < i ; j++ )
				if( strcmp(cmd->cmds[j], cp->cmds[j])){
					skip=1;
					break;
				}
			if(skip)
				continue;

			if( !strcmp(cmd->cmds[i], cp->cmds[i]))
				continue;
			for(s2 = cp->cmds[i], s1 = argv[i+1];
				*s1 == *s2 && *s1; s1++, s2++ ) ;
			if( !*s1 )
				match++;
		}
		if(match){
			int j;
			fprintf(stderr, "ERROR: in command '");
			for( j = 0 ; j <= i ; j++ )
				fprintf(stderr, "%s%s",j?" ":"", argv[j+1]);
			fprintf(stderr, "', '%s' is ambiguous\n",argv[j]);
			return -2;
		}
	}
	return 0;
}

/*
 * This function, compacts the program name and the command in the first
 * element of the '*av' array
 */
static int prepare_args(int *ac, char ***av, char *prgname, struct Command *cmd ){

	char	**ret;
	int	i;
	char	*newname;

	ret = (char **)malloc(sizeof(char*)*(*ac+1));
	newname = (char*)malloc(strlen(prgname)+strlen(cmd->verb)+2);
	if( !ret || !newname ){
		free(ret);
		free(newname);
		return -1;
	}

	ret[0] = newname;
	for(i=0; i < *ac ; i++ )
		ret[i+1] = (*av)[i];

	strcpy(newname, prgname);
	strcat(newname, " ");
	strcat(newname, cmd->verb);

	(*ac)++;
	*av = ret;

	return 0;

}

/*
	This function performs the following jobs:
	- show the help if '--help' or 'help' or '-h' are passed
	- verify that a command is not ambiguous, otherwise show which
	  part of the command is ambiguous
	- if after a (even partial) command there is '--help' show detailed help
	  for all the matching commands
	- if the command doesn't match show an error
	- finally, if a command matches, they return which command matched and
	  the arguments

	The function return 0 in case of help is requested; <0 in case
	of uncorrect command; >0 in case of matching commands
	argc, argv are the arg-counter and arg-vector (input)
	*nargs_ is the number of the arguments after the command (output)
	**cmd_  is the invoked command (output)
	***args_ are the arguments after the command

*/
static int parse_args(int argc, char **argv,
		      CommandFunction *func_,
		      int *nargs_, char **cmd_, char ***args_ )
{
	struct Command	*cp;
	struct Command	*matchcmd = NULL;
	char		*prgname = get_prgname(argv[0]);
	int		i=0, helprequested=0;

	if(argc < 2 || !strcmp(argv[1], "help") ||
		!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")){
		help(prgname);
		return 0;
	}

	if(!strcmp(argv[1], "ver") || !strcmp(argv[1], "-ver") || !strcmp(argv[1], "--ver") ||
	   !strcmp(argv[1], "version") || !strcmp(argv[1], "-version") || !strcmp(argv[1], "--version")){
		printf("%s\n", VERSION);
		return 0;
	}

	for( cp = commands; cp->verb; cp++ )
		if( !cp->ncmds)
			cp->ncmds = split_command(cp->verb, &(cp->cmds));

	for( cp = commands; cp->verb; cp++ ){
		int     match;

		if( argc-1 < cp->ncmds )
			continue;
		for( match = 1, i = 0 ; i < cp->ncmds ; i++ ){
			char	*s1, *s2;
			s1 = cp->cmds[i];
			s2 = argv[i+1];

			for(s2 = cp->cmds[i], s1 = argv[i+1];
				*s1 == *s2 && *s1;
				s1++, s2++ ) ;
			if( *s1 ){
				match=0;
				break;
			}
		}

		/* If you understand why this code works ...
			you are a genious !! */
		if(argc>i+1 && !strcmp(argv[i+1],"--help")){
			if(!helprequested)
				printf("Usage:\n");
			print_help(cp);
			helprequested=1;
			continue;
		}

		if(!match)
			continue;

		matchcmd = cp;
		*nargs_  = argc-matchcmd->ncmds-1;
		*cmd_ = matchcmd->verb;
		*args_ = argv+matchcmd->ncmds+1;
		*func_ = cp->func;

		break;
	}

	if(helprequested){
		printf("\n%s\n", VERSION);
		return 0;
	}

	if(!matchcmd){
		fprintf( stderr, "ERROR: unknown command '%s'\n",argv[1]);
		help(prgname);
		return -1;
	}

	if(check_ambiguity(matchcmd, argv))
		return -2;

	/* check the number of argument */
	if (matchcmd->nargs < 0 && matchcmd->nargs < -*nargs_ ){
		fprintf(stderr, "ERROR: '%s' requires minimum %d arg(s)\n",
			matchcmd->verb, -matchcmd->nargs);
			return -2;
	}
	if(matchcmd->nargs >= 0 && matchcmd->nargs != *nargs_ && matchcmd->nargs != 999){
		fprintf(stderr, "ERROR: '%s' requires %d arg(s)\n",
			matchcmd->verb, matchcmd->nargs);
			return -2;
	}
	
        if (prepare_args( nargs_, args_, prgname, matchcmd )){
                fprintf(stderr, "ERROR: not enough memory\\n");
		return -20;
        }


	return 1;
}

void print_usage(CommandFunction func)
{
	int num_commands = ARRAY_SIZE(commands);
	int i;

	for (i = 0; i < num_commands; i++) {
		if (commands[i].func == func) {
			print_help(&commands[i]);
			return;
		}
	}

	fprintf(stderr, "Error: Command not found for the given function pointer.\n");
}

int main(int ac, char **av )
{
	char *cmd = NULL, **args = NULL;
	int nargs = 0, r;
	CommandFunction func = NULL;

	printf("\n┌───────────────────────────────────────────────┐\n");
	printf("│  mmc-utils version: %-24s  │\n", VERSION);
	printf("└───────────────────────────────────────────────┘\n\n");

	r = parse_args(ac, av, &func, &nargs, &cmd, &args);
	if( r <= 0 ){
		/* error or no command to parse*/
		exit(-r);
	}

	exit(func(nargs, args));
}

