#include "imgact_self.h"
#include "vm/vm_map.h"
#include "vm/vm_object.h"
#include "vm/vm_page.h"

typedef enum t_proc_type_4 {
    PTYPE_INVALID=-1,
    PTYPE_BIG_APP=0,
    PTYPE_MINI_APP=1,
    PTYPE_SYSTEM=2,
    PTYPE_NONGAME_MINI_APP=3
} t_proc_type_4;

struct SceProcParam { // SceProcParam
    u_long Size;
    u_int Magic;
    u_int Entry_count;
    u_long SDK_version;
    void *sceProcessName;
    void *sceUserMainThreadName;
    void *sceUserMainThreadPriority;
    void *sceUserMainThreadStackSize;
    struct SceLibcParam *_sceLibcParam;
    void *_sceKernelMemParam;
    struct sceKernelFsParam *_sceKernelFsParam;
    void *sceProcessPreloadEnabled;
    void *field12_0x58;
};

struct image_args {
    char *buf;		/* pointer to string buffer */
    char *begin_argv;	/* beginning of argv in buf */
    char *begin_envv;	/* beginning of envv in buf */
    char *endp;		/* current `end' pointer of arg & env strings */
    char *fname;            /* pointer to filename of executable (system space) */
    char *fname_buf;	/* pointer to optional malloc(M_TEMP) buffer */
    int stringspace;	/* space left in arg & env buffer */
    int argc;		/* count of argument strings */
    int envc;		/* count of environment strings */
    int fd;			/* file descriptor of the executable */
};

struct image_params { // image_params
    struct t_proc *proc;
    void *execlabel;
    void *vp;
    void *object;
    struct t_vattr *attr;
    struct Elf64_Ehdr *image_header;
    void *entry_addr;
    void *reloc_base;
    char vmspace_destroyed;
    char interpreted;
    char opened;
    char field11_0x43;
    int field12_0x44;
    char *interpreter_name;
    Elf64_Auxargs *auxargs;
    void *firstpage;
    void *ps_strings;
    void *auxarg_size;
    struct image_args *args;
    struct sysentvec *sysent;
    char *execpath;
    u_long execpathp;
    char *freepath;
    u_long canary;
    int canarylen;
    int field25_0xa4;
    void *pagesizes;
    int pagesizeslen;
    char stack_prot;
    char field29_0xb5;
    char field30_0xb6;
    char field31_0xb7;
    Elf64_Dyn *dyn_vaddr;
    u_long tls_size;
    u_long tls_align;
    u_long tls_init_size;
    void *tls_init_addr;
    void *eh_frame_hdr_addr;
    u_long eh_frame_hdr_size;
    //struct t_authinfo authinfo;
    void *f_0x178;
    struct SceProcParam *proc_param_ptr;
    size_t proc_param_size;
    void *module_param_ptr;
    long module_param_size;
    int dyn_id;
    int field46_0x1a4;
    long dyn_offset;
    long dyn_filesz;
    int sce_dynlib_data_id;
    int field50_0x1bc;
    void *sce_dynlib_data_ptr;
    long sce_dynlib_data_size;
    int sce_comment_id;
    int field54_0x1d4;
    long sce_comment_offset;
    long *sce_comment_filesz;
    struct vm_object *vm_obj;
    char *_execpath;
    void *min_addr;
    void *max_addr;
    int dyn_exist;
    int field62_0x20c;
    //int rela_???;
    int field64_0x214;
    void *relro_addr;
    long relro_size;
    int game_p_budget;
    //enum t_e_type hdr_e_type;
    short field69_0x22e;
};

int scan_phdr(struct image_params *params,Elf64_Phdr *phdr,u_long phdr_count,int *big_2mb)
{
    void *vaddr;
    int i_m;
    u_long i;
    int data_id;
    u_long p_memsz_gnu_ex;
    int _gnu_ex_id;
    u_long sce_relro_id;
    u_long text_id;
    /*Elf_ProgramHeaderType*/int p_type;

    params->min_addr = (void *)0xffffffffffffffff;
    params->max_addr = (void *)0x0;
    *big_2mb = 0;
    if (phdr_count == 0) {
        text_id._4_4_ = -1;
        _gnu_ex_id = -1;
        data_id = -1;
    }
    else {
        data_id = -1;
        sce_relro_id = 0xffffffff;
        i = 0;
        text_id = 0xffffffff00000000;
        do {
            p_type = phdr[i].p_type;
            i_m = (int)i;
            if ((int)p_type < 0x61000000) {
                if (p_type == PT_LOAD) goto switchD_ffffffff825bd526___pt_load;
                if (p_type != PT_DYNAMIC) {
                    if (p_type == PT_TLS) {
                        params->tls_size = phdr[i].p_memsz;
                        params->tls_align = phdr[i].p_align;
                        params->tls_init_size = phdr[i].p_filesz;
                        params->tls_init_addr = (void *)phdr[i].p_vaddr;
                        if (phdr[i].p_memsz < phdr[i].p_filesz) {
                            return 8;
                        }
                        if (0x7fffffff < phdr[i].p_memsz) {
                            return 8;
                        }
                        if (*(int *)((long)&phdr[i].p_offset + 4) != 0) {
                            return 8;
                        }
                        if (0x20 < phdr[i].p_align) {
                            //rtld_dbg_printf("scan_phdr",0x2f4,"ERROR",
                            //                "alignment of segment %d is %lx. it must be less than 32.\n");
                            return 8;
                        }
                    }
                    goto switchD_ffffffff825bd526___next;
                }
                params->dyn_exist = 1;
                params->dyn_id = i_m;
                params->dyn_vaddr = (Elf64_Dyn *)phdr[i].p_vaddr;
                params->dyn_offset = phdr[i].p_offset;
                params->dyn_filesz = phdr[i].p_filesz;
                if (phdr[i].p_memsz < phdr[i].p_filesz) {
                    return 8;
                }
                if (0x7fffffff < phdr[i].p_memsz) {
                    return 8;
                }
            _sce_dynlib_data:
                _gnu_ex_id = *(int *)((long)&phdr[i].p_offset + 4);
            joined_r0xffffffff825bd7d4:
                if (_gnu_ex_id != 0) {
                    return 8;
                }
            }
            else {
                switch(p_type) {
                case PT_SCE_DYNLIBDATA:
                    params->sce_dynlib_data_id = i_m;
                    params->sce_dynlib_data_ptr = (void *)phdr[i].p_offset;
                    params->sce_dynlib_data_size = phdr[i].p_filesz;
                LAB_ffffffff825bd617:
                    if (phdr[i].p_memsz != 0) {
                        return 8;
                    }
                    if (0x7fffffff < phdr[i].p_filesz) {
                        return 8;
                    }
                    goto _sce_dynlib_data;
                case PT_SCE_PROCPARAM:
                    params->proc_param_ptr = (struct SceProcParam *)phdr[i].p_vaddr;
                    params->proc_param_size = phdr[i].p_filesz;
                    break;
                case PT_SCE_MODULEPARAM:
                    params->module_param_ptr = (void *)phdr[i].p_vaddr;
                    params->module_param_size = phdr[i].p_filesz;
                    break;
                case PT_SCE_MODULEPARAM|PT_LOAD:
                case PT_SCE_DYNLIBDATA|PT_NOTE:
                case PT_SCE_DYNLIBDATA|PT_SHLIB:
                case PT_SCE_DYNLIBDATA|PT_PHDR:
                case PT_SCE_DYNLIBDATA|PT_TLS:
                case 0x61000008:
                case 0x61000009:
                case 0x6100000a:
                case 0x6100000b:
                case 0x6100000c:
                case 0x6100000d:
                case 0x6100000e:
                case 0x6100000f:
                    break;
                case PT_SCE_RELRO:
                switchD_ffffffff825bd526___pt_load:
                    if ((((phdr[i].p_align & 0x3fff) != 0) ||
                         (vaddr = (void *)phdr[i].p_vaddr, ((u_long)vaddr & 0x3fff) != 0)) ||
                        ((phdr[i].p_offset & 0x3fff) != 0)) {
                        //rtld_dbg_printf("scan_phdr",0x2b9,"ERROR","segment #%d of \"%s\" is not page aligned.\n"
                        //                ,i,params->execpath);
                        return 8;
                    }
                    p_memsz_gnu_ex = phdr[i].p_memsz;
                    if (p_memsz_gnu_ex < phdr[i].p_filesz) {
                        return 8;
                    }
                    if (0x7fffffff < p_memsz_gnu_ex) {
                        return 8;
                    }
                    if (phdr[i].p_offset >> 0x20 != 0) {
                        return 8;
                    }
                    if (vaddr < params->min_addr) {
                        params->min_addr = vaddr;
                        p_memsz_gnu_ex = phdr[i].p_memsz;
                        vaddr = (void *)phdr[i].p_vaddr;
                    }
                    vaddr = (void *)((long)vaddr + p_memsz_gnu_ex + 0x3fff & 0xffffffffffffc000);
                    if (params->max_addr < vaddr) {
                        params->max_addr = vaddr;
                        p_memsz_gnu_ex = phdr[i].p_memsz;
                    }
                    if (0x1fffff < p_memsz_gnu_ex) {
                        *big_2mb = 1;
                    }
                    if (phdr[i].p_type == PT_SCE_RELRO) {
                        sce_relro_id = i & 0xffffffff;
                    }
                    else if ((phdr[i].p_flags & 1) == 0) {
                        if (data_id == -1) {
                            data_id = i_m;
                        }
                    }
                    else {
                        text_id = i << 0x20;
                    }
                    break;
                default:
                    if (p_type != PT_GNU_EH_FRAME) {
                        if (p_type == PT_SCE_COMMENT) {
                            params->sce_comment_id = i_m;
                            params->sce_comment_offset = phdr[i].p_offset;
                            params->sce_comment_filesz = (long *)phdr[i].p_filesz;
                            goto LAB_ffffffff825bd617;
                        }
                        break;
                    }
                    params->eh_frame_hdr_addr = (void *)phdr[i].p_vaddr;
                    params->eh_frame_hdr_size = phdr[i].p_memsz;
                    if (phdr[i].p_memsz < phdr[i].p_filesz) {
                        return 8;
                    }
                    if (0x7fffffff < phdr[i].p_memsz) {
                        return 8;
                    }
                    _gnu_ex_id = *(int *)((long)&phdr[i].p_offset + 4);
                    goto joined_r0xffffffff825bd7d4;
                }
            }
        switchD_ffffffff825bd526___next:
            _gnu_ex_id = (int)sce_relro_id;
            i = (u_long)(i_m + 1);
        } while (i < phdr_count);
    }
    if (params->min_addr == (void *)0xffffffffffffffff) {
        return 0x16;
    }
    if (params->max_addr == (void *)0x0) {
        return 0x16;
    }
    if (params->dyn_exist != 0) {
        if (params->sce_dynlib_data_size == 0) {
            return 0x16;
        }
        if (params->dyn_filesz == 0) {
            return 0x16;
        }
    }
    if (_gnu_ex_id != -1) {
        i = phdr[_gnu_ex_id].p_vaddr;
        if (i == 0) {
            return 0x16;
        }
        p_memsz_gnu_ex = phdr[_gnu_ex_id].p_memsz;
        if (p_memsz_gnu_ex == 0) {
            return 0x16;
        }
        if (((phdr[text_id._4_4_].p_memsz + 0x1fffff + phdr[text_id._4_4_].p_vaddr & 0xffffffffffe00000)
             != i) &&
            ((phdr[text_id._4_4_].p_memsz + 0x3fff + phdr[text_id._4_4_].p_vaddr & 0xffffffffffffc000) !=
             i)) {
            return 0x16;
        }
        if (((p_memsz_gnu_ex + 0x1fffff + i & 0xffffffffffe00000) != phdr[data_id].p_vaddr) &&
            ((p_memsz_gnu_ex + 0x3fff + i & 0xffffffffffffc000) != phdr[data_id].p_vaddr)) {
            return 0x16;
        }
    }
    return 0;
}

#define bool int
#define false 0
#define true 1

typedef enum T_M2MB_MODE { // T_M2MB_MODE
    M2MB_DEFAULT=0,
    M2MB_DISABLE=1,
    M2MB_READONLY=2,
    M2MB_ENABLE=3
} T_M2MB_MODE;


bool is_used_mode_2mb(Elf64_Phdr *phdr,int is_dynlib,t_proc_type_4 budget_ptype,T_M2MB_MODE mode_2mb
                      ,int g_self_fixed)

{
    u_int flag_write;
    bool ret1;

    if (budget_ptype == PTYPE_BIG_APP) {
        flag_write = 2;
        if (phdr->p_type != PT_SCE_RELRO) {
            flag_write = phdr->p_flags & 2;
        }
          // mode=1 then ret1 = false
        ret1 = false;
        switch(mode_2mb) {
        case M2MB_DEFAULT:
            ret1 = is_dynlib == 0 && g_self_fixed != 0;
            break;
        case M2MB_DISABLE:
            break;
        case M2MB_READONLY:
                            // read only???
            ret1 = flag_write == 0;
            break;
        case M2MB_ENABLE:
                          // anyway true
            ret1 = true;
            break;
        default:
            //dbg_dump1();
            //dbg_dump3((void *)0xffffffff825bd9b5);
            //                                      // WARNING: Subroutine does not return
            //panic(0x23,"unknown 2mb mode");
        }
    }
    else {
        ret1 = false;
    }
    return ret1;
}



u_long self_load_section_relro
    (vmspace *map,vm_object *obj,long vaddr,void *vaddr2,u_long memsz,u_long p_filesz,
     char p_flags,int used_mode_2m,char *str)

{
    t_proc *ptVar1;
    u_int uVar2;
    vm_object *obj_00;
    int ret2;
    u_long *puVar3;
    u_long limit;
    u_long used;
    long lVar4;
    long lVar5;
    u_long len;
    long lVar6;
    long size;
    u_long uVar7;
    u_long uVar8;
    u_long uVar9;
    u_long __end;
    GS_OFFSET *in_GS_OFFSET;
    u_long local_48;

    len = 0x16;
    if (((u_long)vaddr2 & 0x3fff) == 0) {
        len = memsz + 0x3fff + (long)vaddr2;
        __end = len & 0xffffffffffffc000;
        obj_00 = vm_object_allocate(OBJT_DEFAULT,__end - (long)vaddr2,obj->vm_container,
                                    obj->budget_ptype);
        if (obj_00 == (vm_object *)0x0) {
            len = 0x23;
        }
        else {
            if (((obj_00->vm_container == vmc_game) &&
                 (ptVar1 = in_GS_OFFSET->pc_curthread->td_proc, ptVar1->budget_ptype == PTYPE_BIG_APP)) &&
                (((ptVar1->APPINFO).mmap_flags & 1) != 0)) {
                if (g_self_loading == 0) {
                    if ((Extended_start == 0) && (ExtendedPage != 0)) {
                        uVar7 = memsz + 0x3fff & 0xffffffffffffc000;
                        lVar4 = 0;
                        uVar8 = uVar7 + (long)vaddr2;
                        size = __end - uVar8;
                        if (memsz <= uVar7) {
                            len = uVar8;
                            size = lVar4;
                        }
                        uVar9 = (long)vaddr2 + 0x1fffffU & 0xffffffffffe00000;
                        lVar5 = (uVar8 & 0xffffffffffe00000) - uVar9;
                        if ((uVar8 & 0xffffffffffe00000) < uVar9) {
                            lVar5 = lVar4;
                        }
                        uVar8 = uVar7 + 0x1fffff + (long)vaddr2 & 0xffffffffffe00000;
                        lVar6 = (len & 0xffffffffffe00000) - uVar8;
                        if ((len & 0xffffffffffe00000) < uVar8) {
                            lVar6 = lVar4;
                        }
                        used = lVar6 + lVar5;
                        limit = _2mpage_budget_reserved();
                        if (limit < used) {
                            used = _2mpage_budget_reserved();
                        }
                        limit = 0;
                        if (used_mode_2m != 0) {
                            limit = used;
                        }
                        u_long_ffffffff844d8488 = u_long_ffffffff844d8488 + limit;
                        size = (size + uVar7) - limit;
                        u_long_ffffffff844d8490 = u_long_ffffffff844d8490 + size;
                        limit = vm_budget_limit(PTYPE_BIG_APP,field_mlock);
                        used = vm_budget_used(PTYPE_BIG_APP,field_mlock);
                        if (limit < used + size) {
                            vm_budget_release_extended_mlock();
                            u_long_ffffffff844d8490 = u_long_ffffffff844d8490 - 0x800000;
                        }
                    }
                }
                else {
                    uVar7 = memsz + 0x3fff & 0xffffffffffffc000;
                    lVar4 = 0;
                    uVar8 = uVar7 + (long)vaddr2;
                    size = __end - uVar8;
                    if (memsz <= uVar7) {
                        len = uVar8;
                        size = lVar4;
                    }
                    uVar9 = (long)vaddr2 + 0x1fffffU & 0xffffffffffe00000;
                    lVar5 = (uVar8 & 0xffffffffffe00000) - uVar9;
                    if ((uVar8 & 0xffffffffffe00000) < uVar9) {
                        lVar5 = lVar4;
                    }
                    uVar8 = uVar7 + 0x1fffff + (long)vaddr2 & 0xffffffffffe00000;
                    lVar6 = (len & 0xffffffffffe00000) - uVar8;
                    if ((len & 0xffffffffffe00000) < uVar8) {
                        lVar6 = lVar4;
                    }
                    used = lVar6 + lVar5;
                    limit = _2mpage_budget_reserved();
                    if (limit < used) {
                        used = _2mpage_budget_reserved();
                    }
                    limit = 0;
                    if (used_mode_2m != 0) {
                        limit = used;
                    }
                    u_long_ffffffff844d8488 = u_long_ffffffff844d8488 + limit;
                    u_long_ffffffff844d8490 = u_long_ffffffff844d8490 + ((size + uVar7) - limit);
                }
            }
            vm_object_reference(obj_00);
            _vm_map_lock(&map->vm_map,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x945
                         );
            vm_map_delete(&map->vm_map,vaddr2,__end);
            ret2 = vm_map_insert(map,obj_00,0,(u_long)vaddr2,__end,(u_int)p_flags,
                                 VM_PROT_GPU_ALL|VM_PROT_WRITE|VM_PROT_READ,0,0);
            vm_object_deallocate(obj_00);
            if (ret2 == 0) {
                vm_map_set_name_locked((vm_map_entry *)map,(u_long)vaddr2,__end,str);
                if ((used_mode_2m != 0) && (obj_00->vm_container == vmc_game)) {
                    vm_map_set_2mb_flag(map,vaddr2,__end);
                }
                _vm_map_unlock(&map->vm_map,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                               0x95e);
                ret2 = vm_map_wire(map,(u_long)vaddr2,__end,VM_MAP_WIRE_LOCK|VM_MAP_WIRE_USER);
                if (ret2 == 0) {
                    size = vaddr << 0x20;
                    len = p_filesz;
                    if (0x3fff < p_filesz) {
                        len = (u_long)((u_int)p_filesz & 0x3fff);
                        local_48 = p_filesz;
                        do {
                            puVar3 = vm_imgact_hold_page(obj,size);
                            if (puVar3 == (u_long *)0x0) {
                                return 5;
                            }
                            lVar5 = (long)(int)_DMPML4I;
                            __end = (u_long)G_PMAP_SHIFT;
                            lVar4 = puVar3[8];
                            dbg_printf(in_GS_OFFSET->pc_curthread,"copyout",0);
                            uVar2 = copyout((void *)((lVar5 << 0x1e | __end << 0x27 | 0xffff800000000000) + lVar4)
                                            ,vaddr2,0x4000);
                            _mtx_lock_flags((mtx *)(pa_lock[0].align +
                                                   ((u_long)((u_int)((u_long)puVar3[8] >> 0xe) & 0x7f80) - 0x20)),0,
                                            "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x972);
                            vm_page_deactivate(puVar3);
                            vm_page_unhold(puVar3);
                            _mtx_unlock_flags((mtx *)(pa_lock[0].align +
                                                     ((u_long)((u_int)((u_long)puVar3[8] >> 0xe) & 0x7f80) - 0x20)),0
                                              ,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                                              0x975);
                            if (uVar2 != 0) {
                                return (u_long)uVar2;
                            }
                            vaddr2 = (void *)((long)vaddr2 + 0x4000);
                            size = size + 0x4000;
                            local_48 = local_48 - 0x4000;
                        } while (0x3fff < local_48);
                    }
                    if (len != 0) {
                        puVar3 = vm_imgact_hold_page(obj,size);
                        if (puVar3 == (u_long *)0x0) {
                            return 5;
                        }
                        __end = (u_long)G_PMAP_SHIFT;
                        uVar8 = (u_long)_DMPML4I;
                        size = puVar3[8];
                        dbg_printf(in_GS_OFFSET->pc_curthread,"copyout",0);
                        uVar2 = copyout((void *)((uVar8 << 0x1e | __end << 0x27 | 0xffff800000000000) + size),
                                        vaddr2,len);
                        _mtx_lock_flags((mtx *)(pa_lock[0].align +
                                               ((u_long)((u_int)((u_long)puVar3[8] >> 0xe) & 0x7f80) - 0x20)),0,
                                        "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x987);
                        vm_page_deactivate(puVar3);
                        vm_page_unhold(puVar3);
                        _mtx_unlock_flags((mtx *)(pa_lock[0].align +
                                                 ((u_long)((u_int)((u_long)puVar3[8] >> 0xe) & 0x7f80) - 0x20)),0,
                                          "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x98a);
                        if (uVar2 != 0) {
                            return (u_long)uVar2;
                        }
                    }
                    ret2 = 0;
                }
                len = vm_mmap_to_errno(ret2);
                return len;
            }
            _vm_map_unlock(&map->vm_map,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                           0x955);
            len = 0x16;
        }
    }
    return len;
}



u_long self_load_section(t_authmgr_obj *self_info,vmspace *map,vm_object *obj,u_long id,u_long vaddr,
                        u_long memsz,u_long filesz,char prot,int self_fixed,int used_mode_2m,char *str)

{
    u_int uVar1;
    int ret1;
    vm_object *obj_00;
    u_long *puVar2;
    u_long uVar3;
    u_long reserved;
    long size3;
    u_long uVar4;
    u_long _end;
    char *__format;
    u_long size1;
    long size2;
    u_long uVar5;
    u_long uVar6;
    u_long uVar7;
    u_long size6;
    u_long len;
    GS_OFFSET *in_GS_OFFSET;
    u_long _start;
    u_long min_size;
    t_proc *proc;
    long size4;
    long size5;

    if (memsz < filesz) {
        printf("[KERNEL] %s: memsz(%lx) < filesz(%lx) at segment %d\n","self_load_section",memsz,filesz)
        ;
    }
    else {
        if ((prot & 6) == 6) {
            __format = "[KERNEL] %s: writeable text segment %d, %lx\n";
        }
        else {
            if ((vaddr & 0x3fff) == 0) {
                if (((obj->vm_container == vmc_game) &&
                     (proc = in_GS_OFFSET->pc_curthread->td_proc, proc->budget_ptype == PTYPE_BIG_APP)) &&
                    (((proc->APPINFO).mmap_flags & 1) != 0)) {
                    if (g_self_loading == 0) {
                        if ((Extended_start == 0) && (ExtendedPage != 0)) {
                            min_size = filesz + 0x3fff;
                            if (filesz < memsz) {
                                min_size = filesz;
                            }
                            _start = vaddr + 0x3fff + memsz;
                            size3 = 0;
                            min_size = min_size & 0xffffffffffffc000;
                            size1 = min_size + vaddr;
                            size2 = (_start & 0xffffffffffffc000) - size1;
                            if (memsz <= min_size) {
                                size2 = size3;
                                _start = size1;
                            }
                            _end = vaddr + 0x1fffff & 0xffffffffffe00000;
                            size5 = (size1 & 0xffffffffffe00000) - _end;
                            if ((size1 & 0xffffffffffe00000) < _end) {
                                size5 = size3;
                            }
                            size1 = min_size + 0x1fffff + vaddr & 0xffffffffffe00000;
                            size4 = (_start & 0xffffffffffe00000) - size1;
                            if ((_start & 0xffffffffffe00000) < size1) {
                                size4 = size3;
                            }
                            size6 = size4 + size5;
                            reserved = _2mpage_budget_reserved();
                            if (reserved < size6) {
                                size6 = _2mpage_budget_reserved();
                            }
                            reserved = 0;
                            if (used_mode_2m != 0) {
                                reserved = size6;
                            }
                            u_long_ffffffff844d8488 = u_long_ffffffff844d8488 + reserved;
                            size2 = (size2 + min_size) - reserved;
                            u_long_ffffffff844d8490 = u_long_ffffffff844d8490 + size2;
                            reserved = vm_budget_limit(PTYPE_BIG_APP,field_mlock);
                            size6 = vm_budget_used(PTYPE_BIG_APP,field_mlock);
                            if (reserved < size6 + size2) {
                                vm_budget_release_extended_mlock();
                                u_long_ffffffff844d8490 = u_long_ffffffff844d8490 - 0x800000;
                            }
                        }
                    }
                    else {
                        if (self_fixed != 0) {
                            dbg_dump1();
                            dbg_dump3((void *)0xffffffff825be5ff);
                                                                  // WARNING: Subroutine does not return
                            panic(0x1b,"fixed is TRUE while loading SELF");
                        }
                        min_size = filesz + 0x3fff;
                        if (filesz < memsz) {
                            min_size = filesz;
                        }
                        _start = vaddr + 0x3fff + memsz;
                        size3 = 0;
                        min_size = min_size & 0xffffffffffffc000;
                        size1 = min_size + vaddr;
                        size2 = (_start & 0xffffffffffffc000) - size1;
                        if (memsz <= min_size) {
                            size2 = size3;
                            _start = size1;
                        }
                        _end = vaddr + 0x1fffff & 0xffffffffffe00000;
                        size5 = (size1 & 0xffffffffffe00000) - _end;
                        if ((size1 & 0xffffffffffe00000) < _end) {
                            size5 = size3;
                        }
                        size1 = min_size + 0x1fffff + vaddr & 0xffffffffffe00000;
                        size4 = (_start & 0xffffffffffe00000) - size1;
                        if ((_start & 0xffffffffffe00000) < size1) {
                            size4 = size3;
                        }
                        size6 = size4 + size5;
                        reserved = _2mpage_budget_reserved();
                        if (reserved < size6) {
                            size6 = _2mpage_budget_reserved();
                        }
                        reserved = 0;
                        if (used_mode_2m != 0) {
                            reserved = size6;
                        }
                        u_long_ffffffff844d8488 = u_long_ffffffff844d8488 + reserved;
                        u_long_ffffffff844d8490 = u_long_ffffffff844d8490 + ((size2 + min_size) - reserved);
                    }
                }
                if (filesz < memsz) {
                    min_size = 0xffffffffffe00000;
                    if (used_mode_2m == 0) {
                        min_size = 0xffffffffffffc000;
                    }
                    min_size = min_size & filesz;
                }
                else {
                    min_size = filesz + 0x3fff & 0xffffffffffffc000;
                }
                size1 = (id & 0xffffffff) << 32;
                if (min_size != 0) {
                    // (VM_PROT_WRITE -> 0x100 MAP_DISABLE_COREDUMP) ^ (MAP_DISABLE_COREDUMP |
                    // MAP_PREFAULT | MAP_COPY_ON_WRITE)
                    ret1 = self_map_insert(map,obj,size1,vaddr,min_size + vaddr,(u_int)prot,
                                           (prot & 2) << 7 ^
                                               (MAP_PREFAULT_PARTIAL|MAP_PREFAULT|MAP_COPY_ON_WRITE),self_fixed,
                                           used_mode_2m,str);
                    if (ret1 != 0) {
                        return 0x16;
                    }
                    if ((obj->vm_container == vmc_game) &&
                        (ret1 = vm_map_wire(map,vaddr,min_size + vaddr,VM_MAP_WIRE_LOCK|VM_MAP_WIRE_USER),
                         ret1 != 0)) goto _err;
                }
                if (memsz <= min_size) {
                    return 0;
                }
                _start = min_size + vaddr;
                _end = vaddr + 0x3fff + memsz & 0xffffffffffffc000;
                if (_end - _start != 0) {
                    obj_00 = vm_object_allocate(OBJT_DEFAULT,_end - _start >> 0xe,obj->vm_container,
                                                obj->budget_ptype);
                    if (obj_00 == (vm_object *)0x0) {
                        return 0x23;
                    }
                    ret1 = self_map_insert(map,obj_00,0,_start,_end,(u_int)(prot | 2),0,self_fixed,used_mode_2m
                                           ,str);
                    vm_object_deallocate(obj_00);
                    if (ret1 != 0) {
                        return 0x16;
                    }
                    if ((obj->vm_container == vmc_game) &&
                        (ret1 = vm_map_wire(map,_start,_end,VM_MAP_WIRE_LOCK|VM_MAP_WIRE_USER), ret1 != 0)) {
                    _err:
                        min_size = vm_mmap_to_errno(ret1);
                        return min_size;
                    }
                }
                if ((min_size < filesz) && (uVar4 = filesz - min_size, uVar4 != 0)) {
                    uVar7 = 0;
                    uVar6 = uVar4;
                    do {
                        puVar2 = vm_imgact_hold_page(obj,min_size + size1 + uVar7);
                        if (puVar2 == (u_long *)0x0) {
                            return 5;
                        }
                        len = 0x4000;
                        if (uVar6 < 0x4000) {
                            len = uVar6;
                        }
                        uVar5 = (u_long)_DMPML4I;
                        uVar3 = (u_long)G_PMAP_SHIFT;
                        size2 = puVar2[8];
                        dbg_printf(in_GS_OFFSET->pc_curthread,"copyout",0);
                        uVar1 = copyout((void *)((uVar5 << 0x1e | uVar3 << 0x27 | 0xffff800000000000) + size2),
                                        (void *)(_start + uVar7),len);
                        _mtx_lock_flags((mtx *)(pa_lock[0].align +
                                               ((u_long)((u_int)((u_long)puVar2[8] >> 0xe) & 0x7f80) - 0x20)),0,
                                        "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x209);
                        vm_page_deactivate(puVar2);
                        vm_page_unhold(puVar2);
                        _mtx_unlock_flags((mtx *)(pa_lock[0].align +
                                                 ((u_long)((u_int)((u_long)puVar2[8] >> 0xe) & 0x7f80) - 0x20)),0,
                                          "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x20c);
                        if (uVar1 != 0) {
                            return (u_long)uVar1;
                        }
                        uVar7 = uVar7 + 0x4000;
                        uVar6 = uVar6 - 0x4000;
                    } while (uVar7 < uVar4);
                }
                ret1 = vm_map_protect(&map->vm_map,_start,_end,(u_int)prot,0);
                if (ret1 == 0) {
                    return 0;
                }
                dbg_dump1();
                dbg_dump2((void *)0xffffffff825be47a,0xffffffff829f3679);
                                                                          // WARNING: Subroutine does not return
                panic(0x1f,"%s: vm_map_protect failed.","self_load_section");
            }
            __format = "[KERNEL] %s: non-aligned segment %d, %lx\n";
        }
        printf(__format,"self_load_section",id & 0xffffffff,vaddr);
    }
    return 8;
}



int self_slurp_unloadable_segment(image_params *imgp,long id,u_long offset,u_long size,void *dst)

{
    u_long uVar1;
    long id_shl_0x20;
    u_long uVar2;
    u_long uVar3;

    if ((size | offset) >> 0x20 != 0) {
        return 8;
    }
    id_shl_0x20 = id << 0x20;
    uVar1 = id_shl_0x20 + 0x3fff + offset;
    uVar2 = id_shl_0x20 + offset;
    uVar3 = id_shl_0x20 + 0x7ffe + offset;
    if (-1 < (long)uVar1) {
        uVar3 = uVar1;
    }
    uVar1 = (uVar3 & 0xffffffffffffc000) - uVar2;
    if (uVar1 != 0) {
        if ((size + offset ^ offset) < 0x4000) {
            uVar1 = size;
        }
        id_shl_0x20 = vm_imgact_map_page(imgp->vm_obj,uVar2 & 0xffffffffffffc000);
        if (id_shl_0x20 == 0) {
            return 5;
        }
        memcpy(dst,(void *)(((u_long)_DMPML4I << 0x1e | (u_long)G_PMAP_SHIFT << 0x27 | 0xffff800000000000)
                            + *(long *)(id_shl_0x20 + 0x40)),uVar1);
        vm_imgact_unmap_page(id_shl_0x20);
        dst = (void *)((long)dst + uVar1);
        size = size - uVar1;
        uVar2 = uVar3 & 0xffffffffffffc000;
    }
    uVar1 = size;
    if (0x3fff < size) {
        uVar1 = (u_long)((u_int)size & 0x3fff);
        do {
            id_shl_0x20 = vm_imgact_map_page(imgp->vm_obj,uVar2);
            if (id_shl_0x20 == 0) {
                return 5;
            }
            memcpy(dst,(void *)((long)uVar2 % 0x4000 +
                                ((long)(int)_DMPML4I << 0x1e | (u_long)G_PMAP_SHIFT << 0x27 |
                                 0xffff800000000000) + *(long *)(id_shl_0x20 + 0x40)),0x4000);
            vm_imgact_unmap_page(id_shl_0x20);
            size = size - 0x4000;
            dst = (void *)((long)dst + 0x4000);
            uVar2 = uVar2 + 0x4000;
        } while (0x3fff < size);
    }
    if (uVar1 != 0) {
        id_shl_0x20 = vm_imgact_map_page(imgp->vm_obj,uVar2);
        if (id_shl_0x20 == 0) {
            return 5;
        }
        memcpy(dst,(void *)((long)uVar2 % 0x4000 +
                            ((u_long)_DMPML4I << 0x1e | (u_long)G_PMAP_SHIFT << 0x27 | 0xffff800000000000)
                            + *(long *)(id_shl_0x20 + 0x40)),uVar1);
        vm_imgact_unmap_page(id_shl_0x20);
        return 0;
    }
    return 0;
}



u_long preload_loadable_segments(kthread *td,image_params *imgp)

{
    u_short uVar1;
    u_short uVar2;
    t_uio *ptVar3;
    vnode *pvVar4;
    iovec **ppiVar5;
    iovec **ppiVar6;
    u_int uVar7;
    int cmp;
    u_long uVar8;
    void *pvVar9;
    long lVar10;
    t_uio *ptVar11;
    t_uio *ptVar12;
    u_long uVar13;
    char *__format;
    t_uio *ptVar14;
    t_uio *ptVar15;
    t_uio *ptVar16;
    char local_e0 [16];
    t_uio *local_d0;
    u_long local_c8;
    t_ucred *local_c0;
    kthread *local_b8;
    iovec local_b0;
    char local_a0 [16];
    t_uio *local_90;
    t_uio *local_88;
    t_ucred *local_80;
    kthread *local_78;
    char *local_70;
    t_uio *local_68;
    vop_read_args local_60;
    long local_38;
    Elf64_Ehdr *hdr;

    cmp = strncmp(*(char **)(*(long *)(*(long *)((long)imgp->vp + 0x1a8) + 200) + 8),"fuse",5);
    if (cmp != 0) {
        hdr = imgp->image_header;
        ptVar3 = (t_uio *)imgp->attr->va_size;
        cmp = memcmp(hdr,"\x7fELF",4);
        if (cmp == 0) {
            uVar1 = hdr->e_phnum;
            uVar2 = hdr->e_phentsize;
            ptVar15 = (t_uio *)hdr->e_phoff;
            uVar8 = (u_long)uVar1 * (u_long)uVar2;
            if (((((long)ptVar15 < 0) || (ptVar3 <= ptVar15)) ||
                 ((u_long)((long)ptVar3 - (long)ptVar15) < uVar8)) || (0x4000 < (u_int)uVar8)) {
                printf("[KERNEL] ERROR: %s phoff=%lx, filesize=%lx, phsize=%lx\n",
                       "preload_loadable_segments",ptVar15,ptVar3,uVar8);
                uVar8 = 8;
            }
            else {
                pvVar9 = malloc(uVar8,&M_TEMP,2);
                local_e0._0_8_ = local_e0 + 0x30;
                local_e0._8_4_ = 1;
                local_c0 = (t_ucred *)&DAT_00000001;
                local_a0._8_8_ = imgp->vp;
                local_80 = td->td_ucred;
                local_a0._0_8_ = &vop_read_desc;
                local_90 = (t_uio *)local_e0;
                local_88 = (t_uio *)((u_long)local_88 & 0xffffffff00000000);
                local_d0 = ptVar15;
                local_c8 = uVar8;
                local_b8 = td;
                local_b0.iov_base = pvVar9;
                local_b0.iov_len = uVar8;
                uVar7 = VOP_READ_APV(((vnode *)local_a0._8_8_)->v_op,(vop_read_args *)local_a0);
                if (uVar7 == 0) {
                    if (uVar1 != 0) {
                        ptVar15 = (t_uio *)0x1c000000;
                        uVar13 = 0;
                        do {
                            lVar10 = (long)(int)((int)uVar13 * (u_int)uVar2);
                            if (*(int *)((long)pvVar9 + lVar10) == 1) {
                                ptVar16 = *(t_uio **)((long)pvVar9 + lVar10 + 8);
                                ptVar11 = *(t_uio **)((long)pvVar9 + lVar10 + 0x20);
                                if (((ptVar3 <= ptVar16) || ((long)ptVar16 < 0)) ||
                                    ((t_uio *)((long)ptVar3 - (long)ptVar16) < ptVar11)) {
                                    printf("[KERNEL] ERROR: %s segoff=%lx, filesize=%lx, segsize=%lx\n",
                                           "preload_loadable_segments",ptVar16,ptVar3,ptVar11);
                                    uVar8 = 8;
                                    goto LAB_ffffffff825bed3c;
                                }
                                ppiVar5 = &ptVar11->uio_iov;
                                ppiVar6 = &ptVar16->uio_iov;
                                local_a0._0_8_ = local_a0 + 0x30;
                                ptVar14 = imgp->vp;
                                local_70 = (char *)0x0;
                                local_a0._8_4_ = 1;
                                local_90 = (t_uio *)0x0;
                                local_88 = (t_uio *)0x0;
                                local_68 = (t_uio *)0x0;
                                local_80 = (t_ucred *)0x2;
                                local_78 = td;
                                for (; (long)ptVar16 < (long)((long)ppiVar6 + (long)ppiVar5);
                                     ptVar16 = (t_uio *)&ptVar16[0x2aaaa].uio_segflg) {
                                    local_60.a_uio = (t_uio *)local_a0;
                                    ptVar12 = (t_uio *)0x800000;
                                    if (ptVar15 < (t_uio *)0x800000) {
                                        ptVar12 = ptVar15;
                                    }
                                    if (ptVar11 < ptVar12) {
                                        ptVar12 = ptVar11;
                                    }
                                    local_60.a_cred = td->td_ucred;
                                    local_60.desc = &vop_read_desc;
                                    local_60.a_ioflag = 0;
                                    local_90 = ptVar16;
                                    local_88 = ptVar12;
                                    local_68 = ptVar12;
                                    local_60.a_vp = (vnode *)ptVar14;
                                    uVar7 = VOP_READ_APV((vop_vector *)ptVar14->uio_offset,
                                                         (vop_read_args *)(local_a0 + 0x40));
                                    if (uVar7 != 0) {
                                        __format = "[KERNEL] ERROR: %s reading segment %d\n";
                                        goto LAB_ffffffff825bed23;
                                    }
                                    ptVar15 = (t_uio *)((long)ptVar15 - (long)ptVar12);
                                    ptVar11 = (t_uio *)&ptVar11[-0x2aaab].uio_offset;
                                }
                            }
                            uVar8 = 0;
                            if ((ptVar15 == (t_uio *)0x0) || (uVar13 = uVar13 + 1, uVar1 <= uVar13))
                                goto LAB_ffffffff825bed3c;
                        } while( true );
                    }
                    uVar8 = 0;
                }
                else {
                    __format = "[KERNEL] ERROR: %s reading phdr %d\n";
                LAB_ffffffff825bed23:
                    uVar8 = (u_long)uVar7;
                    printf(__format,"preload_loadable_segments",(u_long)uVar7);
                }
            LAB_ffffffff825bed3c:
                free(pvVar9,&M_TEMP);
            }
            goto LAB_ffffffff825bed51;
        }
        pvVar4 = imgp->vp;
        local_a0._0_8_ = local_a0 + 0x40;
        local_60.desc = (char **)0x0;
        local_60.a_vp = (vnode *)0x0;
        local_a0._8_4_ = 1;
        local_90 = (t_uio *)0x0;
        local_88 = (t_uio *)0x0;
        local_80 = (t_ucred *)0x2;
        local_78 = td;
        if (0 < (long)ptVar3) {
            ptVar11 = (t_uio *)0x1c000000;
            ptVar15 = (t_uio *)0x0;
            ptVar16 = ptVar3;
            do {
                local_d0 = (t_uio *)local_a0;
                ptVar14 = (t_uio *)0x800000;
                if (ptVar11 < (t_uio *)0x800000) {
                    ptVar14 = ptVar11;
                }
                if (ptVar16 < ptVar14) {
                    ptVar14 = ptVar16;
                }
                local_c0 = td->td_ucred;
                local_e0._0_8_ = &vop_read_desc;
                local_c8 = local_c8 & 0xffffffff00000000;
                local_e0._8_8_ = pvVar4;
                local_90 = ptVar15;
                local_88 = ptVar14;
                local_60.a_vp = (vnode *)ptVar14;
                uVar8 = VOP_READ_APV(pvVar4->v_op,(vop_read_args *)local_e0);
                if ((int)uVar8 != 0) goto LAB_ffffffff825bed51;
                ptVar11 = (t_uio *)((long)ptVar11 - (long)ptVar14);
                ptVar15 = (t_uio *)&ptVar15[0x2aaaa].uio_segflg;
                ptVar16 = (t_uio *)&ptVar16[-0x2aaab].uio_offset;
            } while ((long)ptVar15 < (long)ptVar3);
            uVar8 = 0;
            goto LAB_ffffffff825bed51;
        }
    }
    uVar8 = 0;
LAB_ffffffff825bed51:
    if (___stack_chk_guard == local_38) {
        return uVar8;
    }
      // WARNING: Subroutine does not return
    __stack_chk_fail();
}



int self_modevent(u_long param_1,int param_2,execsw *execsw)

{
    int iVar1;
    char *__format;

    if (param_2 == 1) {
        iVar1 = exec_unregister(execsw);
        if (iVar1 == 0) {
            return 0;
        }
        __format = "self unregister failed\n";
    }
    else {
        if (param_2 != 0) {
            return 0x2d;
        }
        iVar1 = exec_register(execsw);
        if (iVar1 == 0) {
            return 0;
        }
        __format = "selfregister failed\n";
    }
    printf(__format);
    return iVar1;
}



// WARNING: Enum "t_sce_qaf_qw": Some values do not have unique names

int exec_self_imgact(struct image_params *params)

{
    Elf64_Ehdr *peVar1;
    struct vm_object *obj;
    struct mtx *m;
    long lVar2;
    void *pvVar3;
    void *pvVar4;
    bool _2mb_mode;
    struct vm_object *vm_obj;
    t_authmgr_obj *self_info;
    u_int big_app_flag;
    int use_mode_2mb;
    long base;
    Elf64_Auxargs *auxargs;
    char *pcVar5;
    long i;
    u_long uVar6;
    long offset;
    u_long uVar7;
    Elf64_Phdr *phdr;
    u_long p_vaddr;
    Elf64_Phdr *entry;
    char bVar8;
    GS_OFFSET *in_GS_OFFSET;
    bool aslr;
    bool used_mode_2m;
    void *text_addr;
    void *data_addr;
    u_long text_size;
    struct vmspace *vmspace;
    u_long max_size;
    u_long mx2_size;
    int razor_gpu;
    int ret1;
    int big_2mb;
    char **pa_debug;
    void *local_48;
    int local_40;
    long local_38;
    Elf64_Phdr *_phdr;
    int dyn_exist;
    int *_flags;
    char *execpath;
    u_long id;
    t_proc_type_4 p_budget;
    u_long p_filesz;
    char *p_map_flags;
    u_long p_memsz;
    Elf_ProgramHeaderType p_type;
    void *reloc_base;
    u_long relro_size;
    kthread *td;

    params->relro_addr = (void *)0x0;
    params->relro_size = 0;
    peVar1 = params->image_header;
    if (peVar1->e_ident[0] == '\x7f') {
        if (((peVar1->e_ident[1] == 'E') && (peVar1->e_ident[2] == 'L')) && (peVar1->e_ident[3] == 'F'))
            goto __next;
    }
    else if (((peVar1->e_ident[0] == 'O') && (peVar1->e_ident[1] == '\x15')) &&
             ((peVar1->e_ident[2] == '=' && (peVar1->e_ident[3] == '\x1d')))) {
    __next:
        ret1 = 0;
        td = in_GS_OFFSET->pc_curthread;
        vm_obj = vm_pager_allocate(OBJT_SELF,params->vp,0,1,0,td->td_ucred);
        use_mode_2mb = 8;
        if (vm_obj == (vm_object *)0x0) goto __exit;
        execpath = td->td_proc->execpath;
        if (execpath == (char *)0x0) {
            execpath = params->execpath;
        }
        obj = *(vm_object **)((long)params->vp + 0x1a8);
        if ((obj->u).vnp.name[0] == '\0') {
            vm_object_set_name(obj,execpath);
        }
        _mtx_lock_flags(&vm_obj->mtx,0,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                        0x3a2);
        self_info = activate_self_info(vm_obj,&ret1);
        if (self_info == (t_authmgr_obj *)0x0) {
            ret1 = 8;
            vm_object_deallocate(vm_obj);
            use_mode_2mb = ret1;
            goto __exit;
        }
        ret1 = sceSblAuthMgrIsLoadable
            (self_info->ctx_id,&td->td_ucred->cr_authinfo,execpath,&params->authinfo);
        if (ret1 == 0) {
            _mtx_lock_flags(&vm_obj->mtx,0,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                            0x3ba);
            p_budget = td->td_proc->budget_ptype;
            params->game_p_budget = p_budget;
            vm_object_set_budget(vm_obj,(t_proc_type_1)p_budget);
            vmspace = (vmspace *)CONCAT44(vmspace._4_4_,p_budget);
            vm_obj->vm_container = p_budget == PTYPE_BIG_APP;
            _mtx_unlock_flags(&vm_obj->mtx,0,
                              "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x3be);
            peVar1 = self_info->elf_data;
            big_app_flag = *(u_int *)&peVar1->e_type;
            if ((((u_short)(big_app_flag + 0x200) < 0x11) &&
                 ((0x10003UL >> ((u_long)(big_app_flag + 0x200 & 0xffff) & 0x3f) & 1) != 0)) ||
                ((short)big_app_flag == 2)) {
                params->dyn_id = 0;
                params->dyn_offset = 0;
                params->dyn_filesz = 0;
                params->sce_dynlib_data_id = 0;
                params->sce_dynlib_data_ptr = (void *)0x0;
                params->sce_dynlib_data_size = 0;
                params->sce_comment_id = 0;
                params->sce_comment_offset = 0;
                params->sce_comment_filesz = (long *)0x0;
                phdr = (Elf64_Phdr *)(peVar1 + 1);
                params->vm_obj = vm_obj;
                params->dyn_exist = 0;
                params->_execpath = params->execpath;
                params->rela____ = 0;
                params->min_addr = (void *)0x0;
                params->max_addr = (void *)0x0;
                params->hdr_e_type = peVar1->e_type;
                ret1 = scan_phdr(params,phdr,(u_long)peVar1->e_phnum,&big_2mb);
                if (ret1 == 0) {
                    if ((params->dyn_exist == 0) && (peVar1->e_type == ET_SCE_DYNEXEC)) {
                        rtld_dbg_printf("exec_self_imgact",0x3e6,"ERROR","illegal ELF file image %s\n",
                                        params->execpath);
                        goto _exit_8;
                    }
                    if (peVar1->e_type == ET_SCE_REPLAY_EXEC) {
                        _flags = &(params->proc->APPINFO).mmap_flags;
                        *_flags = *_flags | 2;
                    }
                    bzero(&params->proc->aslr_flags,28);
                    if (p_budget == PTYPE_BIG_APP) {
                        _2mb_mode = (g_2mb_mode | M2MB_DISABLE) == M2MB_ENABLE ||
                                    g_self_loading != 0 && g_2mb_mode == M2MB_DEFAULT;
                    }
                    else {
                        _2mb_mode = false;
                    }
                    if (peVar1->e_type == ET_SCE_DYNEXEC) {
                        pa_debug = (char **)((u_long)pa_debug & 0xffffffff00000000);
                        big_app_flag = sceRegMgrGetInt(SCE_REGMGR_ENT_KEY_DEVENV_TOOL_pa_debug,(int *)&pa_debug)
                            ;
                        if (big_app_flag != 0) {
                            pa_debug = (char **)((u_long)pa_debug & 0xffffffff00000000);
                        }
                        razor_gpu = 0;
                        big_app_flag = sceRegMgrGetInt(SCE_REGMGR_ENT_KEY_DEVENV_TOOL_razor_gpu,&razor_gpu);
                        if (big_app_flag != 0) {
                            razor_gpu = 0;
                        }
                        use_mode_2mb = dipsw_parameter(is_dev_mode);
                        used_mode_2m = false;
                        if ((use_mode_2mb != 0) && (used_mode_2m = false, (int)pa_debug != 0)) {
                            used_mode_2m = razor_gpu != 0;
                        }
                        use_mode_2mb = sceSblRcMgrIsAllowDisablingAslr();
                        if (use_mode_2mb == 0) {
                            aslr = false;
                        }
                        else {
                            use_mode_2mb = dipsw_parameter(is_disabling_aslr_self);
                            aslr = use_mode_2mb != 0;
                        }
                        if (used_mode_2m || aslr) {
                            params->reloc_base = (void *)0x400000;
                        _inc_reloc:
                            _flags = &params->proc->aslr_flags;
                            *_flags = *_flags | 8;
                            reloc_base = params->reloc_base;
                            params->dyn_vaddr = (Elf64_Dyn *)((long)&params->dyn_vaddr->d_tag + (long)reloc_base);
                            params->tls_init_addr = (void *)((long)params->tls_init_addr + (long)reloc_base);
                            params->eh_frame_hdr_addr =
                                (void *)((long)params->eh_frame_hdr_addr + (long)reloc_base);
                            params->proc_param_ptr =
                                (SceProcParam *)((long)&params->proc_param_ptr->Size + (long)reloc_base);
                            goto LAB_ffffffff825bf427;
                        }
                        aslr_initialize_process_status(params->proc);
                        if (params->max_addr != (void *)0x0 || params->min_addr != (void *)0x0) {
                            reloc_base = (void *)vm_gen_random_page_aligned_addr
                                (0x400000,0x80000000,
                                 (long)params->max_addr - (long)params->min_addr,
                                 _2mb_mode);
                            params->reloc_base = reloc_base;
                            ret1 = 0;
                            goto _inc_reloc;
                        }
                    }
                    else {
                    LAB_ffffffff825bf427:
                        if (params->dyn_exist == 0) {
                        _dyn_not_exist:
                            local_48 = params->vp;
                            pa_debug = &vop_unlock_desc;
                            local_40 = 0;
                            VOP_UNLOCK_APV(*(vop_vector **)((long)local_48 + 0x10),&pa_debug);
                            ret1 = exec_new_vmspace(params,&self_orbis_sysvec);
                            params->proc->p_sysent = &self_orbis_sysvec;
                            _vn_lock(params->vp,0x80400,
                                     "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x465);
                            if (ret1 != 0) goto __error;
                            vmspace = params->proc->p_vmspace;
                            if (td->td_proc->budget_ptype == PTYPE_BIG_APP) {
                                big_app_flag = (td->td_proc->APPINFO).mmap_flags & 1;
                                if (big_app_flag != 0) {
                                    u_long_ffffffff844d8488 = 0;
                                    u_long_ffffffff844d8490 = bigapp_size;
                                    if (((g_self_loading == 0) && (Extended_start == 0)) && (ExtendedPage != 0)) {
                                        u_long_ffffffff844d8490 = bigapp_size + 0x800000;
                                    }
                                }
                                if (peVar1->e_phnum == 0) {
                                    mx2_size = 0;
                                    max_size = 0;
                                }
                                else {
                                    relro_size = 0;
                                    max_size = 0;
                                    mx2_size = 0;
                                    entry = phdr;
                                    do {
                                        if (((entry->p_type == PT_SCE_RELRO) || (entry->p_type == PT_LOAD)) &&
                                            (entry->p_memsz != 0)) {
                                            id = entry->p_vaddr;
                                            if (peVar1->e_type == ET_SCE_DYNEXEC) {
                                                id = id + (long)params->reloc_base;
                                            }
                                            p_memsz = id & 0xffffffffffffc000;
                                            id = ((entry->p_memsz + id) - p_memsz) + 0x3fff & 0xffffffffffffc000;
                                            max_size = max_size + id;
                                            use_mode_2mb = is_used_mode_2mb(entry,0,params->proc->budget_ptype,g_2mb_mode,
                                                                            g_self_loading);
                                            if (use_mode_2mb != 0) {
                                                uVar6 = id + p_memsz & 0xffffffffffe00000;
                                                id = p_memsz + 0x1fffff & 0xffffffffffe00000;
                                                base = 0;
                                                if (id <= uVar6) {
                                                    base = uVar6 - id;
                                                }
                                                mx2_size = mx2_size + base;
                                            }
                                        }
                                        relro_size = relro_size + 1;
                                        entry = entry + 1;
                                    } while (relro_size < peVar1->e_phnum);
                                    big_app_flag = (td->td_proc->APPINFO).mmap_flags & 1;
                                }
                                if ((big_app_flag != 0) && (g_self_loading != 0)) {
                                    relro_size = g_2mb_mode_size;
                                    if (mx2_size < g_2mb_mode_size) {
                                        relro_size = mx2_size;
                                    }
                                    id = 0;
                                    if ((g_2mb_mode | M2MB_DISABLE) == M2MB_ENABLE) {
                                        id = relro_size;
                                    }
                                    p_memsz = game_fmem_size + max_size;
                                    if (bigapp_max_fmem_size < p_memsz - id) {
                                        ret1 = 0xc;
                                        goto __error;
                                    }
                                    if (g_2mb_mode - M2MB_READONLY < 2) {
                                        p_memsz = p_memsz - relro_size;
                                    _disable:
                                        relro_size = 0;
                                    }
                                    else {
                                        if (g_2mb_mode == M2MB_DISABLE) goto _disable;
                                        relro_size = mx2_size;
                                        if (g_2mb_mode != M2MB_DEFAULT) {
                                            dbg_dump1();
                                            dbg_dump3((void *)0xffffffff825bfd48);
                                            // WARNING: Subroutine does not return
                                            panic(0x27,"unknown 2mb mode");
                                        }
                                    }
                                    if (bigapp_max_fmem_size < p_memsz) {
                                        p_memsz = bigapp_max_fmem_size;
                                    }
                                    set_bigapp_limits(p_memsz,relro_size);
                                }
                                if ((g_2mb_mode & ~M2MB_DISABLE) == M2MB_READONLY) {
                                    p_vaddr = _2mpage_budget_reserved();
                                    if (p_vaddr <= mx2_size) {
                                        mx2_size = _2mpage_budget_reserved();
                                    }
                                    p_vaddr = vm_budget_used(PTYPE_BIG_APP,field_mlock);
                                    p_filesz = vm_budget_limit(PTYPE_BIG_APP,field_mlock);
                                    if (p_filesz < p_vaddr + (max_size - mx2_size)) {
                                        ret1 = 0xc;
                                        goto __error;
                                    }
                                }
                            }
                            relro_size = (u_long)peVar1->e_phnum;
                            if (peVar1->e_phnum == 0) {
                                text_size = 0;
                                mx2_size = 0;
                                max_size = 0;
                                text_addr = (void *)0x0;
                                data_addr = (void *)0x0;
                            }
                            else {
                                id = 0;
                                data_addr = (void *)0x0;
                                text_addr = (void *)0x0;
                                max_size = 0;
                                mx2_size = 0;
                                text_size = 0;
                                do {
                                    p_type = phdr->p_type;
                                    if (((p_type == PT_SCE_RELRO) || (p_type == PT_LOAD)) &&
                                        (p_memsz = phdr->p_memsz, p_memsz != 0)) {
                                        bVar8 = 3;
                                        if (p_type != PT_SCE_RELRO) {
                                            bVar8 = (char)phdr->p_flags;
                                            bVar8 = bVar8 >> 2 & 1 | bVar8 & 2 | bVar8 * '\x04' & 4;
                                        }
                                        p_vaddr = phdr->p_vaddr;
                                        if (peVar1->e_type == ET_SCE_DYNEXEC) {
                                            p_vaddr = p_vaddr + (long)params->reloc_base;
                                        }
                                        if ((p_type == PT_SCE_RELRO) && (params->proc->budget_ptype == PTYPE_BIG_APP)) {
                                            p_filesz = phdr->p_filesz;
                                            if (_2mb_mode == false) {
                                                used_mode_2m = false;
                                            }
                                            else {
                                                use_mode_2mb = is_used_mode_2mb(phdr,0,PTYPE_BIG_APP,g_2mb_mode,
                                                                                g_self_loading);
                                                used_mode_2m = use_mode_2mb != 0;
                                            }
                                            ret1 = self_load_section_relro
                                                (vmspace,vm_obj,id & 0xffffffff,p_vaddr,p_memsz,p_filesz,
                                                 bVar8,(u_int)used_mode_2m,"executable");
                                        }
                                        else {
                                            relro_size = phdr->p_filesz;
                                            if (_2mb_mode == false) {
                                                used_mode_2m = false;
                                            }
                                            else {
                                                use_mode_2mb = is_used_mode_2mb(phdr,0,params->proc->budget_ptype,g_2mb_mode
                                                                                ,g_self_loading);
                                                used_mode_2m = use_mode_2mb != 0;
                                            }
                                            relro_size = self_load_section(self_info,vmspace,vm_obj,id & 0xffffffff,
                                                                           p_vaddr,p_memsz,relro_size,bVar8,0,
                                                                           (u_int)used_mode_2m,"executable");
                                            ret1 = (int)relro_size;
                                        }
                                        if (ret1 != 0) goto __error;
                                        reloc_base = (void *)(p_vaddr & 0xffffffffffffc000);
                                        relro_size = (u_long)((u_int)p_vaddr & 0x3fff) + 0x3fff + phdr->p_memsz &
                                                     0xffffffffffffc000;
                                        if ((((phdr->p_flags & 1) == 0) ||
                                             (pvVar3 = reloc_base, pvVar4 = data_addr, p_memsz = relro_size,
                                              uVar6 = mx2_size, relro_size <= text_size)) &&
                                            (pvVar3 = text_addr, pvVar4 = reloc_base, p_memsz = text_size,
                                             uVar6 = relro_size, phdr->p_type == PT_SCE_RELRO)) {
                                            params->relro_size = relro_size;
                                            params->relro_addr = reloc_base;
                                            pvVar4 = data_addr;
                                            uVar6 = mx2_size;
                                        }
                                        mx2_size = uVar6;
                                        text_size = p_memsz;
                                        data_addr = pvVar4;
                                        text_addr = pvVar3;
                                        max_size = max_size + relro_size;
                                        relro_size = (u_long)peVar1->e_phnum;
                                    }
                                    id = id + 1;
                                    phdr = phdr + 1;
                                } while (id < relro_size);
                            }
                            m = *(mtx **)((long)params->vp + 0x1a8);
                            _mtx_lock_flags(m,0,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                                            0x521);
                            _mtx_unlock_flags(m,0,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                                              0x52d);
                            if (data_addr == (void *)0x0 && mx2_size == 0) {
                                data_addr = text_addr;
                                mx2_size = text_size;
                            }
                            reloc_base = (void *)0x0;
                            if (peVar1->e_type == ET_SCE_DYNEXEC) {
                                reloc_base = text_addr;
                            }
                            reloc_base = (void *)((long)reloc_base + peVar1->e_entry);
                            _mtx_lock_flags(&params->proc->p_mtx,0,
                                            "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x542);
                            relro_size = lim_cur(params->proc,2);
                            if ((((relro_size < mx2_size) || (g_maxtsiz < text_size)) ||
                                 (relro_size = lim_cur(params->proc,10), relro_size < max_size)) ||
                                ((use_mode_2mb = racct_set(params->proc,1,mx2_size), use_mode_2mb != 0 ||
                                                                                           (use_mode_2mb = racct_set(params->proc,8,max_size), use_mode_2mb != 0)))) {
                                _mtx_unlock_flags(&params->proc->p_mtx,0,
                                                  "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                                                  0x548);
                                ret1 = 0xc;
                            }
                            else {
                                vmspace->vm_tsize = text_size >> 0xe;
                                vmspace->vm_taddr = (u_long)text_addr;
                                vmspace->vm_dsize = mx2_size >> 0xe;
                                vmspace->vm_daddr = (u_long)data_addr;
                                p_vaddr = params->proc->p_vmspace->vm_daddr;
                                base = lim_rlimit(params->proc,2);
                                _mtx_unlock_flags(&params->proc->p_mtx,0,
                                                  "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                                                  0x55b);
                                params->entry_addr = reloc_base;
                                auxargs = malloc(0x40,&M_TEMP,2);
                                auxargs->execfd = -1;
                                auxargs->phdr = 0;
                                auxargs->phent = (u_long)peVar1->e_phentsize;
                                auxargs->phnum = (u_long)peVar1->e_phnum;
                                auxargs->pagesz = 0x4000;
                                auxargs->base = p_vaddr + 0x3fff + base & 0xffffffffffffc000;
                                auxargs->flags = 0;
                                auxargs->entry = (u_long)reloc_base;
                                params->auxargs = auxargs;
                                params->interpreted = 0;
                                params->proc->p_osrel = 0xdbbcc;
                                strlcpy(params->proc->prog_name,params->execpath,0x400);
                                reloc_base = params->relro_addr;
                                if ((reloc_base == (void *)0x0) || (params->relro_size == 0))
                                    goto LAB_ffffffff825bf15c;
                                ret1 = vm_map_protect(&vmspace->vm_map,(u_long)reloc_base,
                                                      params->relro_size + (long)reloc_base,VM_PROT_READ,0);
                                ret1 = vm_mmap_to_errno(ret1);
                                if (ret1 == 0) {
                                    ret1 = 0;
                                    goto _dynlib_proc_init;
                                }
                            }
                            goto __error;
                        }
                        if ((u_long)peVar1->e_phnum != 0) {
                            i = 0;
                            offset = (long)params->dyn_id * 0x38;
                            base = offset + 8;
                            lVar2 = offset + 0x20;
                            _phdr = (Elf64_Phdr *)(peVar1[1].e_ident + 8);
                            do {
                                if (((offset != 0) && (*(u_long *)_phdr <= *(u_long *)(peVar1[1].e_ident + base))) &&
                                    (*(long *)(peVar1[1].e_ident + lVar2) + *(u_long *)(peVar1[1].e_ident + base) <=
                                     *(u_long *)_phdr + _phdr->p_paddr)) {
                                    if ((int)i != 1) {
                                        params->dyn_id = -(int)i;
                                        params->dyn_offset = params->dyn_offset - *(long *)_phdr;
                                        if (peVar1->e_type != ET_EXEC) goto _dyn_not_exist;
                                        ret1 = 8;
                                        goto __error;
                                    }
                                    break;
                                }
                                i = i + -1;
                                _phdr = _phdr + 1;
                                offset = offset + -0x38;
                            } while (-i != (u_long)peVar1->e_phnum);
                        }
                    }
                    ret1 = 0x16;
                }
                else {
                    _2mb_mode = false;
                    rtld_dbg_printf("exec_self_imgact",0x3e0,"ERROR","found illegal segment header in %s.\n",
                                    params->execpath);
                LAB_ffffffff825bf15c:
                    if (ret1 == 0) {
                    _dynlib_proc_init:
                        dyn_exist = params->dyn_exist;
                        ret1 = dynlib_proc_initialize_step1(params->proc,params);
                        if (dyn_exist == 0) {
                            if (ret1 == 0) {
                                ret1 = dynlib_proc_initialize_step2(params->proc,params);
                                if (ret1 == 0) {
                                LAB_ffffffff825bf63f:
                                    use_mode_2mb = dynlib_copy_executable_sdk_version(params->proc);
                                    if (use_mode_2mb != 0) {
                                        rtld_dbg_printf("exec_self_imgact",0x5be,"ERROR",
                                                        "sdk version is not found in %s.\n",params->execpath);
                                    }
                                    goto __error;
                                }
                                execpath = params->execpath;
                                pcVar5 = "file=%s dynlib_proc_initialize_step2() returned %x\n";
                                uVar7 = 0x59c;
                            }
                            else {
                                execpath = params->execpath;
                                pcVar5 = "file=%s dynlib_proc_initialize_step1() returned %x\n";
                                uVar7 = 0x595;
                            }
                        }
                        else if (ret1 == 0) {
                            ret1 = dynlib_proc_initialize_step2(params->proc,params);
                            if (ret1 == 0) {
                                local_48 = params->vp;
                                pa_debug = &vop_unlock_desc;
                                local_40 = 0;
                                VOP_UNLOCK_APV(*(vop_vector **)((long)local_48 + 0x10),&pa_debug);
                                _vn_lock(params->vp,0x80400,
                                         "W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x5b5);
                                if (ret1 == 0) goto LAB_ffffffff825bf63f;
                                execpath = params->execpath;
                                pcVar5 = "Failed to exec %s result=%x\n";
                                uVar7 = 0x5b8;
                            }
                            else {
                                execpath = params->execpath;
                                pcVar5 = "file=%s dynlib_proc_initialize_step2() returned %x\n";
                                uVar7 = 0x5af;
                            }
                        }
                        else {
                            execpath = params->execpath;
                            pcVar5 = "file=%s dynlib_proc_initialize_step1() returned %x\n";
                            uVar7 = 0x5a8;
                        }
                        rtld_dbg_printf("exec_self_imgact",uVar7,"ERROR",pcVar5,execpath,ret1);
                    }
                }
            }
            else {
                rtld_dbg_printf("exec_self_imgact",0x3c7,"ERROR","Unsupported ELF e_type. %s %x\n",
                                params->execpath,big_app_flag & 0xffff);
            _exit_8:
                _2mb_mode = false;
                ret1 = 8;
            }
        }
        else {
            if (ret1 != 0x62) {
                ret1 = 8;
            }
            _2mb_mode = false;
        }
    __error:
        deactivate_self_info(self_info);
        vm_object_deallocate(vm_obj);
        (params->proc->p_vmspace->vm_map).sdk_version = params->proc->sdk_version;
        if ((_2mb_mode == false) || (g_2mb_mode != M2MB_DEFAULT)) {
        LAB_ffffffff825bf577:
            use_mode_2mb = ret1;
            if (ret1 != 0) goto __exit;
        }
        else {
            free_reserved_fmem_page();
            use_mode_2mb = ret1;
            if (ret1 != 0) goto __exit;
            big_app_flag = (vmspace->vm_map).sdk_version;
            if (big_app_flag < 0x5500000) {
                uVar7 = 1;
            LAB_ffffffff825bf56d:
                ret1 = vm_map_unset_2mb_flag(vmspace,uVar7);
                goto LAB_ffffffff825bf577;
            }
            if (big_app_flag < 0x6000000) {
                uVar7 = 0;
                goto LAB_ffffffff825bf56d;
            }
        }
        use_mode_2mb = 0;
        if (params->proc->p_vm_container == vmc_game) {
            _vm_map_lock(&vmspace->vm_map,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                         0x5d9);
            p_map_flags = &(vmspace->vm_map).flags;
            *p_map_flags = *p_map_flags | 5;
            _vm_map_unlock(&vmspace->vm_map,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c"
                           ,0x5dc);
            use_mode_2mb = ret1;
        }
        goto __exit;
    }
    ret1 = 8;
    use_mode_2mb = 8;
__exit:
    if (___stack_chk_guard == local_38) {
        return use_mode_2mb;
    }
      // WARNING: Subroutine does not return
    __stack_chk_fail();
}



int self_map_insert(vmspace *map,vm_object *obj,u_long offset,u_long start,u_long end,u_int prot,
                    vm_cow_int cow,int self_fixed,int used_mode_2m,char *str)

{
    int iVar1;
    vm_prot_1 max;
    GS_OFFSET *in_GS_OFFSET;
    t_proc *proc;

    if (end < start) {
        dbg_dump1();
        dbg_dump4((void *)0xffffffff825c00d1,0xffffffff829f374b,end,start);
                                                                              // WARNING: Subroutine does not return
        panic(0x13,"[KERNEL] %s: WARNING: end(%lx) < start(%lx)\n","self_map_insert",end,start);
    }
    if ((((obj->vm_container == vmc_game) &&
          (proc = in_GS_OFFSET->pc_curthread->td_proc, proc->budget_ptype == PTYPE_BIG_APP)) &&
         (self_fixed != 0)) && ((((proc->APPINFO).mmap_flags & 1U) != 0 && (g_self_loading != 0)))) {
        dbg_dump1();
        dbg_dump3((void *)0xffffffff825c0118);
                                              // WARNING: Subroutine does not return
        panic(0x17,"fixed is TRUE while loading SELF");
    }
    vm_object_reference(obj);
    _vm_map_lock(&map->vm_map,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x15f);
    if (self_fixed != 0) {
        vm_map_delete(&map->vm_map,start,end);
    }
    max = VM_PROT_GPU_READ|VM_PROT_EXECUTE|VM_PROT_READ;
    if ((prot & 4) == 0) {
        max = VM_PROT_GPU_ALL|VM_PROT_WRITE|VM_PROT_READ;
    }
    iVar1 = vm_map_insert(map,obj,offset,start,end,prot & 0xff,max,cow,0);
    if (iVar1 == 0) {
        vm_map_set_name_locked((vm_map_entry *)map,start,end,str);
        if ((used_mode_2m != 0) && (obj->vm_container == vmc_game)) {
            vm_map_set_2mb_flag(map,start,end);
        }
        _vm_map_unlock(&map->vm_map,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x172
                       );
    }
    else {
        _vm_map_unlock(&map->vm_map,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x172
                       );
        vm_object_deallocate(obj);
    }
    return iVar1;
}

int self_orbis_fixup(void **stack_base,image_params *imgp)

{
    u_int uVar1;
    u_long extraout_RDX;
    void *pos;
    long lVar2;
    long lVar3;
    elf64_Auxargs *auxargs;
    void *base;

    auxargs = imgp->auxargs;
    base = *stack_base;
    pos = (void *)((long)base + (long)(imgp->args->argc + 2 + imgp->args->envc) * 8);
    if (auxargs->execfd != -1) {
        suword(pos,2);
        lVar2 = (long)pos + 8;
        pos = (void *)((long)pos + 0x10);
        suword(lVar2,auxargs->execfd);
    }
    suword(pos,3);
    suword((long)pos + 8,auxargs->phdr);
    suword((long)pos + 0x10,4);
    suword((long)pos + 0x18,auxargs->phent);
    suword((long)pos + 0x20,5);
    suword((long)pos + 0x28,auxargs->phnum);
    suword((long)pos + 0x30,6);
    suword((long)pos + 0x38,auxargs->pagesz);
    suword((long)pos + 0x40,8);
    suword((long)pos + 0x48,auxargs->flags);
    suword((long)pos + 0x50,9);
    suword((long)pos + 0x58,auxargs->entry);
    suword((long)pos + 0x60,7);
    lVar2 = (long)pos + 0x70;
    suword((long)pos + 0x68,auxargs->base);
    if (imgp->execpathp != 0) {
        suword(lVar2,0xf);
        lVar2 = (long)pos + 0x80;
        suword((long)pos + 0x78,imgp->execpathp);
    }
    suword(lVar2,0x12);
    lVar3 = lVar2 + 0x10;
    suword(lVar2 + 8,(long)osreldate);
    if (imgp->canary != 0) {
        suword(lVar3,0x10);
        suword(lVar2 + 0x18,imgp->canary);
        suword(lVar2 + 0x20,0x11);
        lVar3 = lVar2 + 0x30;
        suword(lVar2 + 0x28,(long)imgp->canarylen);
    }
    suword(lVar3,0x13);
    lVar2 = lVar3 + 0x10;
    suword(lVar3 + 8,(long)mp_ncpus);
    if (imgp->pagesizes != (void *)0x0) {
        suword(lVar2,0x14);
        suword(lVar3 + 0x18,imgp->pagesizes);
        suword(lVar3 + 0x20,0x15);
        lVar2 = lVar3 + 0x30;
        suword(lVar3 + 0x28,(long)imgp->pagesizeslen);
    }
    suword(lVar2,0x17);
    if ((imgp->sysent->sv_shared_page_obj == (vm_object *)0x0) ||
        (uVar1 = (u_int)imgp->stack_prot, imgp->stack_prot == 0)) {
        uVar1 = imgp->sysent->sv_stackprot;
    }
    suword(lVar2 + 8,(long)(int)uVar1,extraout_RDX,uVar1);
    suword(lVar2 + 0x10,0);
    suword(lVar2 + 0x18,0);
    free(imgp->auxargs,&M_TEMP);
    imgp->auxargs = (elf64_Auxargs *)0x0;
    base = (void *)((long)base + -8);
    suword(base,(long)imgp->args->argc);
    *stack_base = base;
    return 0;
}



int is_system_path(char *path)

{
    u_int uVar1;
    int iVar2;
    size_t len;
    int ret1;
    GS_OFFSET *in_GS_OFFSET;
    char path_common [1032];
    long local_30;
    kthread *td;

    len = strlen("/system/");
    ret1 = strncmp(path,"/system/",len);
    if (ret1 != 0) {
        td = in_GS_OFFSET->pc_curthread;
        uVar1 = snprintf(path_common,0x400,"/%s/%s/lib/",td->td_proc->p_randomized_path,"common");
        if (uVar1 < 0x400) {
            ret1 = strncmp(path,path_common,(u_long)uVar1);
            if (ret1 == 0) goto _ret_1;
        }
        uVar1 = snprintf(path_common,0x400,"/%s/%s/lib/",td->td_proc->p_randomized_path,"priv");
        ret1 = 0;
        if (0x3ff < uVar1) goto _ret_0;
        iVar2 = strncmp(path,path_common,(u_long)uVar1);
        ret1 = 0;
        if (iVar2 != 0) goto _ret_0;
    }
_ret_1:
    ret1 = 1;
_ret_0:
    return ret1;
}



// WARNING: Type propagation algorithm not settling

int self_load_shared_object(t_proc *proc,char *path,t_lib_info *new,u_long wire)

{
    long lVar1;
    vmspace *map;
    u_int flags;
    long lVar2;
    u_short rtld_flags;
    int is_system;
    vm_object *obj;
    t_authmgr_obj *self_info;
    u_long reloc_base;
    u_long reserved;
    u_long qVar3;
    u_long uVar4;
    long addr_delta;
    long lVar5;
    u_long uVar6;
    long rela_ofs;
    u_int __flags;
    char bVar7;
    Elf64_Phdr *_phdr;
    GS_OFFSET *in_GS_OFFSET;
    bool is_use_2mb_mode;
    void *rel_addr;
    void *text_addr;
    size_t rel_memsz;
    u_long relro_size;
    void *relro_addr;
    u_long phnum_count;
    u_long p_memsz_align;
    void *section_addr;
    long max_size;
    u_long text_size;
    Elf64_Phdr *phdr;
    t_proc_type_4 game_p_budget;
    u_long id;
    char *start_addr;
    t_vattr tStack_4a8;
    char *local_400;
    int local_3f8;
    u_long local_3f0;
    u_long local_3e8;
    u_long local_3d0;
    u_long local_3c8;
    vnode *vp;
    u_long local_398;
    u_long local_390;
    kthread *local_388;
    int big_2mb;
    int open_ret;
    vop_getattr_args info;
    kthread *local_328;
    u_long local_320;
    char execpath_local [40];
    image_params imgp;
    t_authinfo data;
    elf64_hdr *hdr;
    t_proc_type_4 p_budget;
    char *p_execpath;
    u_long p_memsz;
    char *p_prog_name;
    Elf_ProgramHeaderType p_type;
    char *prtld_flags;
    void *relro_addr2;
    kthread *td;
    bool use_2mb_mode;

    lVar2 = ___stack_chk_guard;
    td = in_GS_OFFSET->pc_curthread;
    local_398 = 0;
    local_390 = 0x5200044;
    local_3f8 = 1;
    open_ret = 0;
    local_3d0 = 0xffffff9c;
    local_3e8 = 0;
    local_3f0 = 0;
    local_3c8 = 0;
    local_400 = path;
    local_388 = td;
    open_ret = namei(&local_400);
    if (open_ret != 0) {
        is_system = open_ret;
        if (open_ret != 13) goto __exit;
        p_execpath = "namei() error (path=%s)\n";
        uVar6 = 0x63f;
    LAB_ffffffff825bc202:
        rtld_dbg_printf("self_load_shared_object",uVar6,"ERROR",p_execpath,path);
        is_system = open_ret;
        goto __exit;
    }
    info.a_cred = td->td_ucred;
    info.a_vap = &tStack_4a8;
    info.desc = &vop_getattr_desc;
    info.a_vp = vp;
    open_ret = VOP_GETATTR_APV(vp->v_op,&info);
    if (open_ret != 0) {
    LAB_ffffffff825bc275:
        NDFREE((long)&local_400,0);
        is_system = open_ret;
        goto __exit;
    }
    if ((((vp->v_mount->mnt_flag & 4) != 0) || ((tStack_4a8.va_mode & 0x49U) == 0)) ||
        (tStack_4a8.va_type != VREG)) {
        NDFREE((long)&local_400,0);
        rtld_dbg_printf("self_load_shared_object",0x656,"ERROR",
                        "mount flag / attribute error (path=%s)\n",path);
        is_system = 0xd;
        if (vp->v_mount != (mount *)0x0) {
            rtld_dbg_printf("self_load_shared_object",0x65a,"ERROR",
                            "mnt_flag 0x%lx  va_mode 0x%x  va_type 0x%x\n",vp->v_mount->mnt_flag,
                            tStack_4a8.va_mode,tStack_4a8.va_type);
        }
        goto __exit;
    }
    if ((u_long)tStack_4a8.va_size < 0x20) {
        NDFREE((long)&local_400,0);
        is_system = 8;
        goto __exit;
    }
    info.a_cred = td->td_ucred;
    info.desc = &vop_access_desc;
    info.a_vp = vp;
    info.a_vap = (t_vattr *)CONCAT44(info.a_vap._4_4_,0x40);
    local_328 = td;
    open_ret = VOP_ACCESS_APV(vp->v_op,&info);
    if (open_ret != 0) {
        NDFREE((long)&local_400,0);
        is_system = open_ret;
        if (open_ret != 0xd) goto __exit;
        p_execpath = "VOP_ACCESS() error (path=%s)\n";
        uVar6 = 0x66b;
        goto LAB_ffffffff825bc202;
    }
    if (vp->v_writecount != 0) {
        NDFREE((long)&local_400,0);
        is_system = 26;
        goto __exit;
    }
    info.a_cred = td->td_ucred;
    info.desc = &vop_open_desc;
    info.a_vp = vp;
    info.a_vap = (t_vattr *)CONCAT44(info.a_vap._4_4_,1);
    local_320 = 0;
    local_328 = td;
    open_ret = VOP_OPEN_APV(vp->v_op,&info);
    if (open_ret != 0) goto LAB_ffffffff825bc275;
    obj = (vp->v_bufobj).bo_object;
    if (obj == (vm_object *)0x0) {
        open_ret = 0xd;
        rtld_dbg_printf("self_load_shared_object",0x684,"ERROR","sprx_object error (path=%s)\n");
    LAB_ffffffff825bc5b0:
        if (open_ret != 0) goto __close;
        obj = vm_pager_allocate(OBJT_SELF,vp,0,1,0,td->td_ucred);
        if (obj == (vm_object *)0x0) {
            open_ret = 8;
            goto __close;
        }
        _mtx_lock_flags(&obj->mtx,0,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",0x6a5
                        );
        self_info = activate_self_info(obj,&open_ret);
        if (self_info == (t_authmgr_obj *)0x0) {
            open_ret = 8;
        }
        else {
            p_execpath = td->td_proc->execpath;
            p_prog_name = td->td_proc->prog_name;
            if (p_execpath != (char *)0x0) {
                p_prog_name = p_execpath;
            }
            open_ret = sceSblAuthMgrIsLoadable
                (self_info->ctx_id,&td->td_ucred->cr_authinfo,p_prog_name,&data);
            if (open_ret == 0) {
                _mtx_lock_flags(&obj->mtx,0,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c",
                                0x6c1);
                p_budget = td->td_proc->budget_ptype;
                if ((u_long)(data.app_type._7_1_ & 0xf) - 4 < 4) {
                __is_game_module:
                    game_p_budget = p_budget;
                }
                else {
                    if ((data.app_type._7_1_ & 0xf) == 1) {
                        is_system = is_system_path(path);
                        if (is_system != 0) {
                            p_execpath = get_file_name(path);
                            goto __is_system;
                        }
                        goto __is_game_module;
                    }
                    is_system = is_system_path(path);
                    game_p_budget = PTYPE_SYSTEM;
                    if (is_system != 0) {
                        p_execpath = get_file_name(path);
                    __is_system:
                        game_p_budget = PTYPE_SYSTEM;
                        if ((p_execpath != (char *)0x0) &&
                            ((is_system = strncmp(p_execpath,"libc.sprx",10), is_system == 0 ||
                                                                                    (is_system = strncmp(p_execpath,"libSceFios2.sprx",0x11), is_system == 0))))
                            goto __is_game_module;
                    }
                }
                vm_object_set_budget(obj,(t_proc_type_1)game_p_budget);
                obj->vm_container = game_p_budget == PTYPE_BIG_APP;
                _mtx_unlock_flags(&obj->mtx,0,"W:\\Build\\J02608488\\sys\\freebsd\\sys\\kern\\imgact_self.c"
                                  ,0x6c5);
                hdr = self_info->elf_data;
                if (hdr->e_type != ET_SCE_DYNAMIC) {
                    rtld_dbg_printf("self_load_shared_object",0x6ca,"ERROR","Unsupported ELF e_type. %s %x\n",
                                    path);
                    goto _exit_8;
                }
                phdr = (Elf64_Phdr *)(hdr + 1);
                bzero(&imgp,0x230);
                imgp.dyn_exist = 0;
                imgp.hdr_e_type = hdr->e_type;
                imgp.proc = proc;
                imgp.vm_obj = obj;
                imgp._execpath = path;
                open_ret = scan_phdr(&imgp,phdr,(u_long)hdr->e_phnum,&big_2mb);
                if (open_ret == 0) {
                    if (imgp.dyn_exist == 1) {
                        new->tls_size = imgp.tls_size;
                        new->tls_align = imgp.tls_align;
                        new->tls_init_size = imgp.tls_init_size;
                        new->tls_init_addr = imgp.tls_init_addr;
                        new->eh_frame_hdr_addr = imgp.eh_frame_hdr_addr;
                        new->eh_frame_hdr_size = imgp.eh_frame_hdr_size;
                        if ((u_long)hdr->e_phnum == 0) {
                        _err_22:
                            open_ret = 22;
                        }
                        else {
                            lVar5 = 0;
                            rela_ofs = (long)imgp.dyn_id * 0x38;
                            addr_delta = rela_ofs + 8;
                            lVar1 = rela_ofs + 0x20;
                            p_execpath = hdr[1].e_ident + 8;
                        LAB_ffffffff825bc9c4:
                            if (((rela_ofs == 0) ||
                                 (*(u_long *)(hdr[1].e_ident + addr_delta) < *(u_long *)p_execpath)) ||
                                (*(u_long *)p_execpath + *(u_long *)(p_execpath + 0x18) <
                                 *(long *)(hdr[1].e_ident + lVar1) + *(u_long *)(hdr[1].e_ident + addr_delta)))
                                goto LAB_ffffffff825bc9b4;
                            if ((int)lVar5 == 1) goto _err_22;
                            imgp.dyn_id = -(int)lVar5;
                            imgp.dyn_offset = imgp.dyn_offset - *(u_long *)p_execpath;
                            is_system = strncmp(path,"/app0/sce_module/",0x11);
                            if (((is_system == 0) ||
                                 (p_execpath = strstr(path,"/lib/libc.sprx"), p_execpath != (char *)0x0)) ||
                                (p_execpath = strstr(path,"/lib/libSceFios2.sprx"), p_execpath != (char *)0x0)) {
                                rtld_flags = SUB42(new->rtld_flags,0) | 0x400;
                                *(u_short *)&new->rtld_flags = rtld_flags;
                            }
                            else {
                                rtld_flags = *(u_short *)&new->rtld_flags;
                            }
                            use_2mb_mode = false;
                                                  // libc_fios
                            if ((game_p_budget != PTYPE_BIG_APP) || ((rtld_flags & 0x400) != 0)) goto _start_mmap;
                            if ((g_2mb_mode | M2MB_DISABLE) != M2MB_ENABLE) {
                                use_2mb_mode = false;
                                goto _start_mmap;
                            }
                            use_2mb_mode = true;
                            if (td->td_proc->budget_ptype != PTYPE_BIG_APP) goto _start_mmap;
                            if (hdr->e_phnum == 0) {
                                text_size = 0;
                                max_size = 0;
                            }
                            else {
                                phnum_count = 0;
                                max_size = 0;
                                text_size = 0;
                                _phdr = phdr;
                                do {
                                    if (((_phdr->p_type == PT_SCE_RELRO) || (_phdr->p_type == PT_LOAD)) &&
                                        (_phdr->p_memsz != 0)) {
                                        relro_addr2 = imgp.reloc_base;
                                        if (hdr->e_type != ET_SCE_DYNEXEC) {
                                            relro_addr2 = (void *)0x0;
                                        }
                                        reloc_base = (long)relro_addr2 + _phdr->p_vaddr;
                                        p_memsz = reloc_base & 0xffffffffffffc000;
                                        reloc_base = ((_phdr->p_memsz + reloc_base) - p_memsz) + 0x3fff &
                                                     0xffffffffffffc000;
                                        max_size = max_size + reloc_base;
                                        is_system = is_used_mode_2mb(_phdr,1,(imgp.proc)->budget_ptype,g_2mb_mode,0);
                                        if (is_system != 0) {
                                            uVar4 = reloc_base + p_memsz & 0xffffffffffe00000;
                                            reloc_base = p_memsz + 0x1fffff & 0xffffffffffe00000;
                                            addr_delta = 0;
                                            if (reloc_base <= uVar4) {
                                                addr_delta = uVar4 - reloc_base;
                                            }
                                            text_size = text_size + addr_delta;
                                        }
                                    }
                                    _phdr = _phdr + 1;
                                    phnum_count = phnum_count + 1;
                                } while (phnum_count < hdr->e_phnum);
                            }
                            if ((g_2mb_mode & ~M2MB_DISABLE) != M2MB_READONLY) {
                            _start_mmap:
                                         // not read only???
                                map = proc->p_vmspace;
                                start_addr = &DAT_80000000;
                                __flags = (proc->aslr_flags & 1U) * 0x100000;
                                flags = __flags | 0x31002;
                                if (!use_2mb_mode) {
                                    flags = __flags + 0x21002;
                                }
                                if (game_p_budget == PTYPE_SYSTEM) {
                                    start_addr = &DAT_800000000;
                                }
                                open_ret = vm_mmap(&map->vm_map,(u_long *)&start_addr,
                                                   (long)imgp.max_addr - (long)imgp.min_addr,0,0,flags,0,(int *)0x0,
                                                   0,(void *)0x0);
                                if (open_ret == 0) {
                                    p_prog_name = rindex(path,'/');
                                    p_execpath = p_prog_name + 1;
                                    if (p_prog_name == (char *)0x0) {
                                        p_execpath = path;
                                    }
                                    strlcpy(execpath_local,p_execpath,0x20);
                                    addr_delta = (long)start_addr - (long)imgp.min_addr;
                                    imgp.min_addr = start_addr;
                                    imgp.max_addr = (void *)((long)imgp.max_addr + addr_delta);
                                    new->tls_init_addr = (void *)((long)new->tls_init_addr + addr_delta);
                                    new->eh_frame_hdr_addr = (void *)((long)new->eh_frame_hdr_addr + addr_delta);
                                    if (hdr->e_phnum == 0) {
                                        relro_addr = (void *)0x0;
                                        rel_addr = (void *)0x0;
                                        text_addr = (void *)0x0;
                                        relro_size = 0;
                                        rel_memsz = 0;
                                        text_size = 0;
                                    }
                                    else {
                                        id = 0;
                                        relro_addr = (void *)0x0;
                                        rel_addr = (void *)0x0;
                                        text_addr = (void *)0x0;
                                        relro_size = 0;
                                        rel_memsz = 0;
                                        text_size = 0;
                                        do {
                                            p_type = phdr->p_type;
                                            reloc_base = relro_size;
                                            relro_addr2 = relro_addr;
                                            if (((p_type == PT_SCE_RELRO) || (p_type == PT_LOAD)) &&
                                                (p_memsz = phdr->p_memsz, p_memsz != 0)) {
                                                if (p_type == PT_SCE_RELRO) {
                                                    bVar7 = 3;
                                                    section_addr = (void *)(phdr->p_vaddr + addr_delta);
                                                    p_memsz_align = p_memsz + 0x3fff & 0xffffffffffffc000;
                                                    if (proc->budget_ptype != PTYPE_BIG_APP) goto LAB_ffffffff825bcff6;
                                                    reserved = phdr->p_filesz;
                                                    if (use_2mb_mode) {
                                                        is_system = is_used_mode_2mb(phdr,1,(imgp.proc)->budget_ptype,g_2mb_mode
                                                                                     ,0);
                                                        is_use_2mb_mode = is_system != 0;
                                                    }
                                                    else {
                                                        is_use_2mb_mode = false;
                                                    }
                                                    open_ret = self_load_section_relro
                                                        (map,obj,id,section_addr,p_memsz,reserved,3,
                                                         (u_int)is_use_2mb_mode,execpath_local);
                                                }
                                                else {
                                                    bVar7 = (char)phdr->p_flags;
                                                    section_addr = (void *)(phdr->p_vaddr + addr_delta);
                                                    bVar7 = bVar7 >> 2 & 1 | bVar7 & 2 | bVar7 * '\x04' & 4;
                                                    p_memsz_align = p_memsz + 0x3fff & 0xffffffffffffc000;
                                                LAB_ffffffff825bcff6:
                                                    uVar4 = phdr->p_filesz;
                                                    if (use_2mb_mode) {
                                                        is_system = is_used_mode_2mb(phdr,1,(imgp.proc)->budget_ptype,g_2mb_mode
                                                                                     ,0);
                                                        is_use_2mb_mode = is_system != 0;
                                                    }
                                                    else {
                                                        is_use_2mb_mode = false;
                                                    }
                                                    p_memsz = self_load_section(self_info,map,obj,id,(u_long)section_addr,
                                                                                p_memsz,uVar4,bVar7,1,(u_int)is_use_2mb_mode,
                                                                                execpath_local);
                                                    open_ret = (int)p_memsz;
                                                }
                                                if (open_ret != 0) goto _deactivate_self_info;
                                                if (((wire & 1) != 0) &&
                                                    (open_ret = vm_map_wire(map,(u_long)section_addr,
                                                                            (long)section_addr + p_memsz_align,
                                                                            VM_MAP_WIRE_USER), open_ret != 0)) goto _exit_12;
                                                if ((text_size < p_memsz_align) && ((phdr->p_flags & 1) != 0)) {
                                                    text_size = p_memsz_align;
                                                    text_addr = section_addr;
                                                }
                                                else {
                                                    reloc_base = p_memsz_align;
                                                    relro_addr2 = section_addr;
                                                    if (phdr->p_type != PT_SCE_RELRO) {
                                                        rel_addr = section_addr;
                                                        rel_memsz = p_memsz_align;
                                                        reloc_base = relro_size;
                                                        relro_addr2 = relro_addr;
                                                    }
                                                }
                                            }
                                            relro_addr = relro_addr2;
                                            relro_size = reloc_base;
                                            phdr = phdr + 1;
                                            id = id + 1;
                                        } while (id < hdr->e_phnum);
                                        if ((relro_size != 0) && (relro_addr != (void *)0x0)) {
                                            open_ret = vm_map_protect(&map->vm_map,(u_long)relro_addr,
                                                                      (long)relro_addr + relro_size,VM_PROT_READ,0);
                                            open_ret = vm_mmap_to_errno(open_ret);
                                        }
                                    }
                                    if (open_ret == 0) {
                                        imgp.dyn_vaddr =
                                            (Elf64_Dyn *)((long)&(imgp.dyn_vaddr)->d_tag + (long)start_addr);
                                        imgp.entry_addr = start_addr + hdr->e_entry;
                                        imgp.tls_init_addr = (void *)((long)imgp.tls_init_addr + (long)start_addr);
                                        if (imgp.eh_frame_hdr_addr != (void *)0x0) {
                                            imgp.eh_frame_hdr_addr =
                                                (void *)((long)imgp.eh_frame_hdr_addr + (long)start_addr);
                                        }
                                        if (rel_addr == (void *)0x0 && rel_memsz == 0) {
                                            rel_addr = text_addr;
                                            rel_memsz = text_size;
                                        }
                                        if (imgp.module_param_ptr != (sceModuleParam *)0x0) {
                                            imgp.module_param_ptr =
                                                (void *)((long)imgp.module_param_ptr + (long)start_addr);
                                        }
                                        open_ret = elf64_get_eh_frame_info
                                            (new->eh_frame_hdr_addr,new->eh_frame_hdr_size,addr_delta,
                                             (long)text_addr + text_size,&new->eh_frame_addr,
                                             &new->eh_frame_size);
                                        if (open_ret != 0) {
                                            new->eh_frame_addr = (void *)0x0;
                                            new->eh_frame_size = 0;
                                        }
                                        new->id = (long)new;
                                        new->mapbase = start_addr;
                                        new->mapsize = (long)imgp.max_addr - (long)imgp.min_addr;
                                        new->text_size = text_size;
                                        new->data_addr = rel_addr;
                                        new->data_size = rel_memsz;
                                        new->relocbase = imgp.min_addr;
                                        new->entry_addr = (void *)(addr_delta + hdr->e_entry);
                                        new->module_param = imgp.module_param_ptr;
                                        new->relro_addr = relro_addr;
                                        new->relro_size = relro_size;
                                        if (game_p_budget == PTYPE_SYSTEM) {
                                            prtld_flags = &(new->rtld_flags).field_0x1;
                                            *prtld_flags = *prtld_flags | 1;
                                        }
                                        open_ret = acquire_per_file_info_obj(&imgp,new);
                                        if (open_ret == 0) {
                                            is_system = is_dump_pfi();
                                            if (is_system != 0) {
                                                dump_pfi(new->rel_data);
                                            }
                                        }
                                        else {
                                            rtld_dbg_printf("self_load_shared_object",0x801,"ERROR",
                                                            "acquire_per_file_info_obj()=%x\n",open_ret);
                                        }
                                    }
                                }
                                else {
                                    rtld_dbg_printf("self_load_shared_object",0x75f,"ERROR",
                                                    "failed to allocate VA for \"%s\": %x\n",path,open_ret);
                                }
                                goto _deactivate_self_info;
                            }
                            reserved = _2mpage_budget_reserved();
                            if (reserved <= text_size) {
                                text_size = _2mpage_budget_reserved();
                            }
                            reserved = vm_budget_used(PTYPE_BIG_APP,field_mlock);
                            qVar3 = vm_budget_limit(PTYPE_BIG_APP,field_mlock);
                            if (reserved + (max_size - text_size) <= qVar3) goto _start_mmap;
                        _exit_12:
                            open_ret = 12;
                        }
                    }
                    else {
                        rtld_dbg_printf("self_load_shared_object",0x6e1,"ERROR",
                                        "missing dynamic segment in %s.\n",imgp._execpath);
                        open_ret = 0x16;
                    }
                }
                else {
                    rtld_dbg_printf("self_load_shared_object",0x6dd,"ERROR",
                                    "found illegal segment header in %s.\n",imgp._execpath);
                }
            }
            else {
                rtld_dbg_printf("self_load_shared_object",0x6bb,"ERROR",
                                "sceSblAuthMgrIsLoadable(%s)=%x error\n",path,open_ret);
                if (open_ret != 0x62) {
                _exit_8:
                    open_ret = 8;
                }
            }
        _deactivate_self_info:
            deactivate_self_info(self_info);
        }
    }
    else {
           // vm_obj exist
        if ((obj->u).vnp.name[0] == '\0') {
            vm_object_set_name(obj,path);
        }
        addr_delta = vm_imgact_map_page(obj,0);
        if (addr_delta != 0) {
            lVar1 = *(long *)(addr_delta + 0x40);
            reloc_base = (u_long)_DMPML4I << 0x1e | (u_long)G_PMAP_SHIFT << 0x27 | 0xffff800000000000;
            if (*(char *)(reloc_base + lVar1) == '\x7f') {
                if (((*(char *)(lVar1 + 1 + reloc_base) != 'E') ||
                     (*(char *)(lVar1 + 2 + reloc_base) != 'L')) ||
                    (open_ret = 0, *(char *)(lVar1 + 3 + reloc_base) != 'F')) goto LAB_ffffffff825bc59d;
            }
            else if (((*(char *)(reloc_base + lVar1) != 'O') ||
                      (*(char *)(lVar1 + 1 + reloc_base) != '\x15')) ||
                     ((*(char *)(lVar1 + 2 + reloc_base) != '=' ||
                       (open_ret = 0, *(char *)(lVar1 + 3 + reloc_base) != '\x1d')))) {
            LAB_ffffffff825bc59d:
                open_ret = 8;
            }
            vm_imgact_unmap_page(addr_delta);
            goto LAB_ffffffff825bc5b0;
        }
        open_ret = 5;
    __close:
        obj = (vm_object *)0x0;
    }
    info.a_cred = td->td_ucred;
    info.desc = &vop_close_desc;
    info.a_vp = vp;
    info.a_vap = (t_vattr *)CONCAT44(info.a_vap._4_4_,1);
    local_328 = td;
    VOP_CLOSE_APV(vp->v_op,&info);
    NDFREE((long)&local_400,0);
    is_system = open_ret;
    if (obj != (vm_object *)0x0) {
        vm_object_deallocate(obj);
        is_system = open_ret;
    }
__exit:
    if (___stack_chk_guard != lVar2) {
        // WARNING: Subroutine does not return
        __stack_chk_fail();
    }
    return is_system;
LAB_ffffffff825bc9b4:
    lVar5 = lVar5 + -1;
    p_execpath = p_execpath + 0x38;
    rela_ofs = rela_ofs + -0x38;
    if (-lVar5 == (u_long)hdr->e_phnum) goto _err_22;
    goto LAB_ffffffff825bc9c4;
}
