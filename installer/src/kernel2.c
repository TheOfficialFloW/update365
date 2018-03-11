#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/threadmgr.h>

#include <taihen.h>

#include <stdio.h>
#include <string.h>

#define MOD_LIST_SIZE 128

int module_get_export_func(SceUID pid, const char *modname, uint32_t libnid, uint32_t funcnid, uintptr_t *func);

int (* _ksceKernelGetModuleList)(SceUID pid, int flags1, int flags2, SceUID *modids, size_t *num);
int (* _ksceKernelGetModuleInfo)(SceUID pid, SceUID modid, SceKernelModuleInfo *info);

void _start() __attribute__ ((weak, alias("module_start")));
int module_start(SceSize args, void *argp) {
	int ret;
	SceKernelModuleInfo info;
	SceUID modlist[MOD_LIST_SIZE];
	int count = MOD_LIST_SIZE;
	int i;

	ret = module_get_export_func(KERNEL_PID, "SceKernelModulemgr", 0xC445FA63, 0x97CF7B4E, (uintptr_t *)&_ksceKernelGetModuleList);
	if (ret < 0)
		ret = module_get_export_func(KERNEL_PID, "SceKernelModulemgr", 0x92C9FFC2, 0xB72C75A4, (uintptr_t *)&_ksceKernelGetModuleList);
	if (ret < 0)
		return SCE_KERNEL_START_FAILED;

	ret = module_get_export_func(KERNEL_PID, "SceKernelModulemgr", 0xC445FA63, 0xD269F915, (uintptr_t *)&_ksceKernelGetModuleInfo);
	if (ret < 0)
		ret = module_get_export_func(KERNEL_PID, "SceKernelModulemgr", 0x92C9FFC2, 0xDAA90093, (uintptr_t *)&_ksceKernelGetModuleInfo);
	if (ret < 0)
		return SCE_KERNEL_START_FAILED;

	ret = _ksceKernelGetModuleList(KERNEL_PID, 0x7FFFFFFF, 1, modlist, &count);
	if (ret < 0)
		return SCE_KERNEL_START_FAILED;

	info.size = sizeof(SceKernelModuleInfo);
	ret = _ksceKernelGetModuleInfo(KERNEL_PID, modlist[1], &info);
	if (ret < 0)
		return SCE_KERNEL_START_FAILED;

	// second last kernel module must be either taihen or HENkaku
	if (strcmp(info.module_name, "taihen") != 0 && strcmp(info.module_name, "HENkaku") != 0)
		return SCE_KERNEL_START_FAILED;

	ret = _ksceKernelGetModuleList(ksceKernelGetProcessId(), 0x7FFFFFFF, 1, modlist, &count);
	if (ret < 0)
		return SCE_KERNEL_START_FAILED;

	info.size = sizeof(SceKernelModuleInfo);
	ret = _ksceKernelGetModuleInfo(ksceKernelGetProcessId(), modlist[0], &info);
	if (ret < 0)
		return SCE_KERNEL_START_FAILED;

	// last user module must be SceAppUtil
	if (strcmp(info.module_name, "SceAppUtil") != 0)
		return SCE_KERNEL_START_FAILED;

	return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp) {
	return SCE_KERNEL_STOP_SUCCESS;
}