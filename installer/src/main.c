#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/libssl.h>
#include <psp2/power.h>
#include <psp2/shellutil.h>
#include <psp2/sysmodule.h>
#include <psp2/io/devctl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/net/http.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <taihen.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <malloc.h>

#include "sha256.h"
#include "pspdebug.h"

#define printf psvDebugScreenPrintf

#define APP_PATH "ux0:app/UPDATE365/"
#define PUP_URL "https://github.com/TheOfficialFloW/update365/releases/download/v0.1/PSP2UPDAT.PUP"

#define CHUNK_SIZE 64 * 1024

static uint8_t psp2updat_pup_hash[0x20] = {
	0x86, 0x85, 0x9B, 0x30, 0x71, 0x68, 0x12, 0x68,
	0xB6, 0xD0, 0xBE, 0xB5, 0xEF, 0x69, 0x1D, 0xA8,
	0x74, 0xB6, 0x72, 0x6E, 0x85, 0xB6, 0xF0, 0x6A,
	0x1C, 0xDF, 0x6A, 0x0E, 0x18, 0x3E, 0x77, 0xC6
};

const char taihen_config_recovery_header[] =
	"# This file is used as an alternative if ux0:tai/config.txt is not found.\n";

const char taihen_config_header[] = 
	"# For users plugins, you must refresh taiHEN from HENkaku Settings for\n"
	"# changes to take place.\n"
	"# For kernel plugins, you must reboot for changes to take place.\n";

const char taihen_config[] = 
	"*KERNEL\n"
	"# henkaku.skprx is hard-coded to load and is not listed here\n"
	"*main\n"
	"# main is a special titleid for SceShell\n";

const char *clean_files[] = {
	"ud0:PSP2UPDATE/PSP2UPDAT.PUP",
	"ud0:PSP2UPDATE/psp2swu.self",
	"ud0:PSP2UPDATE/ENSOUPDAT.PUP",
	"ud0:PSP2UPDATE/ensoswu.self",
};

void clean_ud0() {
	int i;
	for (i = 0; i < (sizeof(clean_files) / sizeof(char **)); i++) {
		sceIoRemove(clean_files[i]);
	}
}

int verify_cleaned() {
	int ret;
	SceIoStat stat;
	
	int i;
	for (i = 0; i < (sizeof(clean_files) / sizeof(char **)); i++) {
		if (sceIoGetstat(clean_files[i], &stat) >= 0) {
			return -1;
		}
	}

	return 0;
}

void ErrorExit(int milisecs, char *fmt, ...) {
	va_list list;
	char msg[256];

	va_start(list, fmt);
	vsprintf(msg, fmt, list);
	va_end(list);

	printf(msg);

	sceKernelPowerUnlock(0);
	clean_ud0();
	sceKernelDelayThread(milisecs * 1000);
	sceKernelExitProcess(0);
}

int write_taihen_config(const char *path, int recovery) {
	int fd;

	// write default config
	sceIoRemove(path);
	fd = sceIoOpen(path, SCE_O_TRUNC | SCE_O_CREAT | SCE_O_WRONLY, 6);
	if (recovery) {
		sceIoWrite(fd, taihen_config_recovery_header, sizeof(taihen_config_recovery_header) - 1);
	}
	sceIoWrite(fd, taihen_config_header, sizeof(taihen_config_header) - 1);
	sceIoWrite(fd, taihen_config, sizeof(taihen_config) - 1);
	sceIoClose(fd);

	return 0;
}

int extract(const char *pup, const char *psp2swu) {
	int inf, outf;

	if ((inf = sceIoOpen(pup, SCE_O_RDONLY, 0)) < 0) {
		return -1;
	}

	if ((outf = sceIoOpen(psp2swu, SCE_O_CREAT | SCE_O_WRONLY | SCE_O_TRUNC, 6)) < 0) {
		return -1;
	}

	int ret = -1;
	int count;

	if (sceIoLseek(inf, 0x18, SCE_SEEK_SET) < 0) {
		goto end;
	}

	if (sceIoRead(inf, &count, 4) < 4) {
		goto end;
	}

	if (sceIoLseek(inf, 0x80, SCE_SEEK_SET) < 0) {
		goto end;
	}

	struct {
		uint64_t id;
		uint64_t off;
		uint64_t len;
		uint64_t field_18;
	} __attribute__((packed)) file_entry;

	for (int i = 0; i < count; i++) {
		if (sceIoRead(inf, &file_entry, sizeof(file_entry)) != sizeof(file_entry)) {
			goto end;
		}

		if (file_entry.id == 0x200) {
			break;
		}
	}

	if (file_entry.id == 0x200) {
		char buffer[1024];
		size_t rd;

		if (sceIoLseek(inf, file_entry.off, SCE_SEEK_SET) < 0) {
			goto end;
		}

		while (file_entry.len && (rd = sceIoRead(inf, buffer, sizeof(buffer))) > 0) {
			if (rd > file_entry.len) {
				rd = file_entry.len;
			}
			sceIoWrite(outf, buffer, rd);
			file_entry.len -= rd;
		}

		if (file_entry.len == 0) {
			ret = 0;
		}
	}

end:
	sceIoClose(inf);
	sceIoClose(outf);
	return ret;
}

int verify(const char *src, const uint8_t *expect_hash) {
	SceUID fd = sceIoOpen(src, SCE_O_RDONLY, 0);
	if (fd < 0)
		return fd;

	void *buf = memalign(4096, CHUNK_SIZE);
	if (!buf) {
		sceIoClose(fd);
		return -1;
	}

	SHA256_CTX ctx;
	memset(&ctx, 0, sizeof(SHA256_CTX));
	sha256_init(&ctx);

	while (1) {
		int read = sceIoRead(fd, buf, CHUNK_SIZE);

		if (read < 0) {
			free(buf);
			sceIoClose(fd);
			return read;
		}

		if (read == 0)
			break;

		sha256_update(&ctx, buf, read);
	}

	free(buf);
	sceIoClose(fd);

	uint8_t hash[32];
	sha256_final(&ctx, hash);
	if (memcmp(hash, expect_hash, sizeof(hash)) != 0) {
		return -1;
	}

	return 0;
}

int copy(const char *src, const char *dst) {
	SceUID fdsrc = sceIoOpen(src, SCE_O_RDONLY, 0);
	if (fdsrc < 0)
		return fdsrc;

	SceUID fddst = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 6);
	if (fddst < 0) {
		sceIoClose(fdsrc);
		return fddst;
	}

	void *buf = memalign(4096, CHUNK_SIZE);
	if (!buf) {
		sceIoClose(fddst);
		sceIoClose(fdsrc);
		return -1;
	}

	while (1) {
		int read = sceIoRead(fdsrc, buf, CHUNK_SIZE);

		if (read < 0) {
			free(buf);

			sceIoClose(fddst);
			sceIoClose(fdsrc);

			sceIoRemove(dst);

			return read;
		}

		if (read == 0)
			break;

		int written = sceIoWrite(fddst, buf, read);

		if (written < 0) {
			free(buf);

			sceIoClose(fddst);
			sceIoClose(fdsrc);

			sceIoRemove(dst);

			return written;
		}
	}

	free(buf);

	sceIoClose(fddst);
	sceIoClose(fdsrc);

	return 0;
}

int download(const char *src, const char *dst) {
	int ret;
	int statusCode;
	int tmplId = -1, connId = -1, reqId = -1;
	SceUID fd = -1;

	ret = sceHttpCreateTemplate("Updater/1.00 libhttp/1.1", SCE_HTTP_VERSION_1_1, SCE_TRUE);
	if (ret < 0)
		goto ERROR_EXIT;

	tmplId = ret;

	ret = sceHttpCreateConnectionWithURL(tmplId, src, SCE_TRUE);
	if (ret < 0)
		goto ERROR_EXIT;

	connId = ret;

	ret = sceHttpCreateRequestWithURL(connId, SCE_HTTP_METHOD_GET, src, 0);
	if (ret < 0)
		goto ERROR_EXIT;

	reqId = ret;

	ret = sceHttpSendRequest(reqId, NULL, 0);
	if (ret < 0)
		goto ERROR_EXIT;

	ret = sceHttpGetStatusCode(reqId, &statusCode);
	if (ret < 0)
		goto ERROR_EXIT;

	if (statusCode == 200) {
		uint8_t buf[4096];
		uint64_t size = 0;
		uint32_t value = 0;

		ret = sceHttpGetResponseContentLength(reqId, &size);
		if (ret < 0)
			goto ERROR_EXIT;

		ret = sceIoOpen(dst, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 6);
		if (ret < 0)
			goto ERROR_EXIT;

		fd = ret;

		int x = psvDebugScreenGetX();
		int y = psvDebugScreenGetY();

		while (1) {
			int read = sceHttpReadData(reqId, buf, sizeof(buf));

			if (read < 0) {
				ret = read;
				break;
			}

			if (read == 0)
				break;

			int written = sceIoWrite(fd, buf, read);

			if (written < 0) {
				ret = written;
				break;
			}

			value += read;

			printf("%d%%", (value * 100) / (uint32_t)size);
			psvDebugScreenSetXY(x, y);
		}
	}

ERROR_EXIT:
	if (fd >= 0)
		sceIoClose(fd);

	if (reqId >= 0)
		sceHttpDeleteRequest(reqId);

	if (connId >= 0)
		sceHttpDeleteConnection(connId);

	if (tmplId >= 0)
		sceHttpDeleteTemplate(tmplId);

	return ret;
}

void init_net() {
	static char memory[16 * 1024];

	sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
	sceSysmoduleLoadModule(SCE_SYSMODULE_HTTPS);

	SceNetInitParam param;
	param.memory = memory;
	param.size = sizeof(memory);
	param.flags = 0;

	sceNetInit(&param);
	sceNetCtlInit();

	sceSslInit(300 * 1024);
	sceHttpInit(40 * 1024);

	sceHttpsDisableOption(SCE_HTTPS_FLAG_SERVER_VERIFY);
}

void finish_net() {
	sceSslTerm();
	sceHttpTerm();
	sceNetCtlTerm();
	sceNetTerm();
	sceSysmoduleUnloadModule(SCE_SYSMODULE_HTTPS);
	sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
}

typedef struct {
	uint32_t off;
	uint32_t sz;
	uint8_t code;
	uint8_t type;
	uint8_t active;
	uint32_t flags;
	uint16_t unk;
} __attribute__((packed)) partition_t;

typedef struct {
	char magic[0x20];
	uint32_t version;
	uint32_t device_size;
	char unk1[0x28];
	partition_t partitions[0x10];
	char unk2[0x5e];
	char unk3[0x10 * 4];
	uint16_t sig;
} __attribute__((packed)) master_block_t;

const char *device = "sdstor0:int-lp-act-entire";

int is_mbr(void *data) {
	master_block_t *master = data;
	if (memcmp(master->magic, "Sony Computer Entertainment Inc.", 0x20) != 0)
		return 0;
	if (master->sig != 0xAA55)
		return 0;
	return 1;
}

int main(int argc, char *argv[]) {
	int ret;
	SceIoStat stat;
	SceUID fd;

	psvDebugScreenInit();
	sceKernelPowerLock(0);
	sceAppMgrUmount("app0:");

	printf("3.65 HENkaku Enso Updater\n\n");

	if (sceIoDevctl("ux0:", 0x3001, NULL, 0, NULL, 0) == 0x80010030) {
		ErrorExit(10000, "Enable unsafe homebrew first before using this software.\n");
	}

	if (scePowerGetBatteryLifePercent() < 50) {
		ErrorExit(10000, "Battery has to be at least at 50%%.\n");
	}

	// detect plugins
	ret = taiLoadStartKernelModule(APP_PATH "kernel2.skprx", 0, NULL, 0);
	if (ret < 0) {
		ErrorExit(20000, "Disable all plugins first before using this software.\n"
											"If you have already disabled them, but still get this message,\n"
											"reboot your device and launch this software again without\n"
											"launching any other applications before.\n"
											"VitaShell or Adrenaline for example start kernel modules.\n");
	}

  ret = taiStopUnloadKernelModule(ret, 0, NULL, 0, NULL, NULL);
	if (ret < 0)
		ErrorExit(10000, "Error 0x%08X unloading kernel2.skprx.\n", ret);

	// detect previous enso
	static master_block_t master;
	fd = sceIoOpen(device, SCE_O_RDONLY, 0777);
	if (fd < 0)
		ErrorExit(10000, "Error 0x%08X opening device.\n", fd);

	if ((ret = sceIoLseek(fd, 0x200, SCE_SEEK_SET)) != 0x200) {
		ErrorExit(10000, "Error 0x%08X seeking device.\n", ret);
	}
	if ((ret = sceIoRead(fd, &master, sizeof(master))) != sizeof(master)) {
		ErrorExit(10000, "Error 0x%08X reading device.\n", ret);
	}

	sceIoClose(fd);

	if (is_mbr(&master)) {
		ErrorExit(20000, "Please uninstall enso first before updating.\n"
											"Tip: Unlink Memory Card in HENkaku Settings first\n"
											"     before you uninstall enso.\n");
	}

	if (sceIoGetstat(APP_PATH "PSP2UPDAT.PUP", &stat) < 0) {
		ErrorExit(10000, "Could not find %s.\n", APP_PATH "PSP2UPDAT.PUP");
/*
		printf("Do you want to download it from the internet? (X=yes, R=no)\n\n");

		while (1) {
			SceCtrlData pad;
			sceCtrlPeekBufferPositive(0, &pad, 1);

			if (pad.buttons & SCE_CTRL_CROSS)
				break;
			else if (pad.buttons & (SCE_CTRL_RTRIGGER | SCE_CTRL_R1))
				ErrorExit(10000, "Canceled by user.\n");

			sceKernelDelayThread(10000);
		}

		printf("Downloading PSP2UPDAT.PUP...");
    init_net();
		ret = download(PUP_URL, APP_PATH "PSP2UPDAT.PUP");
    finish_net();
		if (ret < 0)
			ErrorExit(10000, "Error 0x%08X downloading PSP2UPDAT.PUP.\n", ret);
*/
	}

	printf("You are about to update to Custom Firmware 3.65 HENkaku Enso.\n\n");
	printf("- Note that once updated there is no way to downgrade back to your\n"
					"  current firmware.\n"
					"- Remember to never attempt to modify vs0: or reinstall 3.65 PUP,\n"
					"  otherwise you will lose the ability to run homebrews forever.\n"
					"- Make sure that all your favorite homebrews and plugins are\n"
					"  compatible on 3.65 before updating.\n"
					"- Check if you have VitaShell v1.82 or higher installed yet\n"
					"  and if you have made a CMA backup of it already.\n"
					"  This is very important in case you accidentally lose VitaShell.\n\n");

	printf("Continues in 20 seconds.\n\n");
	sceKernelDelayThread(20 * 1000 * 1000);

	printf("Press X to confirm that you have read and understood all the\n");
	printf("risks and suggestions above, R to exit.\n\n");

	while (1) {
		SceCtrlData pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);

		if (pad.buttons & SCE_CTRL_CROSS)
			break;
		else if (pad.buttons & (SCE_CTRL_RTRIGGER | SCE_CTRL_R1))
			ErrorExit(10000, "Canceled by user.\n");

		sceKernelDelayThread(10000);
	}

	printf("This software will make PERMANENT modifications to your Vita.\n"
					"If anything goes wrong, there is NO RECOVERY (not even with a\n"
					"hardware flasher). The creators provide this tool \"as is\", without\n"
					"warranty of any kind, express or implied and cannot be held\n"
					"liable for any damage done.\n\n");

	printf("Continues in 20 seconds.\n\n");
	sceKernelDelayThread(20 * 1000 * 1000);

	printf("Press X to accept these terms and start the update, R to not accept and exit.\n\n");

	while (1) {
		SceCtrlData pad;
		sceCtrlPeekBufferPositive(0, &pad, 1);

		if (pad.buttons & SCE_CTRL_CROSS)
			break;
		else if (pad.buttons & (SCE_CTRL_RTRIGGER | SCE_CTRL_R1))
			ErrorExit(10000, "Canceled by user.\n");

		sceKernelDelayThread(10000);
	}

	psvDebugScreenClear();
	printf("3.65 HENkaku Enso Updater\n\n");

	printf("Cleaning ud0:...");
	clean_ud0();
	ret = verify_cleaned();
	if (ret < 0)
		ErrorExit(10000, "Error could not clean ud0:.\n");
	printf("OK\n");
	sceKernelDelayThread(500 * 1000);

	sceIoMkdir("ud0:PSP2UPDATE", 6);

	printf("Copying PSP2UPDAT.PUP to ud0:...");
	ret = copy(APP_PATH "PSP2UPDAT.PUP", "ud0:PSP2UPDATE/ENSOUPDAT.PUP");
	if (ret < 0)
		ErrorExit(10000, "Error 0x%08X copying PSP2UPDAT.PUP.\n", ret);
	printf("OK\n");
	sceKernelDelayThread(500 * 1000);

	printf("Verifying PSP2UPDAT.PUP...");
	ret = verify("ud0:PSP2UPDATE/ENSOUPDAT.PUP", psp2updat_pup_hash);
	if (ret < 0)
		ErrorExit(10000, "Error 0x%08X. The file is wrong.\n", ret);
	printf("OK\n");
	sceKernelDelayThread(500 * 1000);

	printf("Extracting psp2swu.self...");
	ret = extract("ud0:PSP2UPDATE/ENSOUPDAT.PUP", "ud0:PSP2UPDATE/ensoswu.self");
	if (ret < 0)
		ErrorExit(10000, "Error 0x%08X extracting psp2swu.self.\n", ret);
	printf("OK\n");
	sceKernelDelayThread(500 * 1000);

	printf("Removing old taiHENkaku files...");

	const char *files[] = {
		"ux0:tai/config.txt",
		"ur0:tai/config.txt",
		"ur0:tai/boot_config.txt",
		"ur0:tai/taihen.skprx",
		"ur0:tai/henkaku.skprx",
		"ur0:tai/henkaku.suprx",
		"ur0:tai/henkaku_config.bin"
	};

	int i;
	for (i = 0; i < (sizeof(files) / sizeof(char **)); i++) {
		sceIoRemove(files[i]);
	}

	printf("OK\n");
	sceKernelDelayThread(500 * 1000);

	printf("Writing new config files...");
	sceIoMkdir("ux0:tai", 6);
	sceIoMkdir("ur0:tai", 6);
	write_taihen_config("ux0:tai/config.txt", 0);
	write_taihen_config("ur0:tai/config.txt", 1);
	printf("OK\n");
	sceKernelDelayThread(500 * 1000);

	printf("\n");
	printf("Please do not press any buttons or power off the device during the\n"
					"update, otherwise you may cause permanent damage to your device.\n\n");
	sceKernelDelayThread(5 * 1000 * 1000);
	printf("Have a safe trip and see you on the other side.\n\n");
	sceKernelDelayThread(3 * 1000 * 1000);
	printf("Best regards,\n");
	sceKernelDelayThread(1 * 1000 * 1000);
	printf("- TheFloW\n\n");
	sceKernelDelayThread(3 * 1000 * 1000);

	printf("Starting SCE updater...\n");
	sceKernelDelayThread(1 * 1000 * 1000);

	sceKernelPowerUnlock(0);

	ret = taiLoadStartKernelModule("ux0:app/UPDATE365/kernel.skprx", 0, NULL, 0);
	if (ret < 0)
		ErrorExit(10000, "Error 0x%08X loading kernel.skprx.\n", ret);

	sceKernelDelayThread(60 * 1000 * 1000);
	ErrorExit(10000, "Error starting SCE updater.\n");

	return 0;
}