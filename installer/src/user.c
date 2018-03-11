#include <psp2/appmgr.h>
#include <psp2/shellutil.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <taihen.h>

static tai_hook_ref_t sceSblSsUpdateMgrGetBootModeRef;
static tai_hook_ref_t sceSblSsUpdateMgrSendCommandRef;
static tai_hook_ref_t sceIoRemoveRef;

static SceUID hooks[6];

static char ux0_data_patch[] = "ux0:/data";
static char ud0_psp2update_ensoswu_self_patch[] = "ud0:/PSP2UPDATE/ensoswu.self";
static char ud0_psp2update_ensoupdat_pup_patch[] = "ud0:/PSP2UPDATE/ENSOUPDAT.PUP";

int sceSblSsUpdateMgrGetBootModePatched(int *mode) {
	int ret;
	ret = TAI_CONTINUE(int, sceSblSsUpdateMgrGetBootModeRef, mode);
	*mode = 0x40; // GUI
	return ret;
}

int sceSblSsUpdateMgrSendCommandPatched(int cmd, int arg) {
	if (cmd == 0 || cmd == 1) {
		sceKernelPowerUnlock(0);
	}

	return TAI_CONTINUE(int, sceSblSsUpdateMgrSendCommandRef, cmd, arg);
}

// when launching with ksceAppMgrLaunchAppByPath the whole directory is blocked,
// so we will get an error when trying to remove a file
int sceIoRemovePatched(const char *file) {
	TAI_CONTINUE(int, sceIoRemoveRef, file);
	return 0; // always return ok
}

void _start() __attribute__ ((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
	int res;
	tai_module_info_t info;

	// lock system
	res = sceKernelPowerLock(0);
	res |= sceShellUtilInitEvents(0);
	res |= sceShellUtilLock(0xFFF);
	res |= sceAppMgrDestroyOtherApp();
	if (res < 0)
		return SCE_KERNEL_START_FAILED;

	info.size = sizeof(tai_module_info_t);
	if (taiGetModuleInfo("ScePsp2Swu", &info) < 0)
		return SCE_KERNEL_START_FAILED;

	hooks[0] = taiHookFunctionImport(&sceSblSsUpdateMgrGetBootModeRef, "ScePsp2Swu", 0x31406C49, 0x8E834565, sceSblSsUpdateMgrGetBootModePatched);
	if (hooks[0] < 0)
		return SCE_KERNEL_START_FAILED;

	hooks[1] = taiHookFunctionImport(&sceSblSsUpdateMgrSendCommandRef, "ScePsp2Swu", 0x31406C49, 0x1825D954, sceSblSsUpdateMgrSendCommandPatched);
	if (hooks[1] < 0)
		return SCE_KERNEL_START_FAILED;

	hooks[2] = taiHookFunctionImport(&sceIoRemoveRef, "ScePsp2Swu", 0xCAE9ACE6, 0xE20ED0F3, sceIoRemovePatched);
	if (hooks[2] < 0)
		return SCE_KERNEL_START_FAILED;

	hooks[3] = taiInjectData(info.modid, 0, 0x2EB408, ux0_data_patch, sizeof(ux0_data_patch));
	if (hooks[3] < 0)
		return SCE_KERNEL_START_FAILED;

	hooks[4] = taiInjectData(info.modid, 0, 0x2EB428, ud0_psp2update_ensoswu_self_patch, sizeof(ud0_psp2update_ensoswu_self_patch));
	if (hooks[4] < 0)
		return SCE_KERNEL_START_FAILED;

	hooks[5] = taiInjectData(info.modid, 0, 0x2EB448, ud0_psp2update_ensoupdat_pup_patch, sizeof(ud0_psp2update_ensoupdat_pup_patch));
	if (hooks[5] < 0)
		return SCE_KERNEL_START_FAILED;

	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
	if (hooks[5] >= 0)
		taiInjectRelease(hooks[5]);
	if (hooks[4] >= 0)
		taiInjectRelease(hooks[4]);
	if (hooks[3] >= 0)
		taiInjectRelease(hooks[3]);
	if (hooks[2] >= 0)
		taiHookRelease(hooks[2], sceIoRemoveRef);
	if (hooks[1] >= 0)
		taiHookRelease(hooks[1], sceSblSsUpdateMgrSendCommandRef);
	if (hooks[0] >= 0)
		taiHookRelease(hooks[0], sceSblSsUpdateMgrGetBootModeRef);

	return SCE_KERNEL_STOP_SUCCESS;
}