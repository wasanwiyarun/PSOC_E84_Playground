#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/printk.h>

FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t mountp={.type=FS_LITTLEFS,.fs_data=&storage,.storage_dev=(void*)FIXED_PARTITION_ID(storage_partition),.mnt_point="/lfs"};
static const struct device *const console_uart = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
static void result(const char*n,int r){printk("%s %s=%d\n",r<0?"ERR":"OK",n,r);}
static void handle(char *s){
 char *a,*data; int r; struct fs_file_t f; char b[128];
 if(!strcmp(s,"fs format")){fs_unmount(&mountp);r=fs_mkfs(FS_LITTLEFS,(uintptr_t)FIXED_PARTITION_ID(storage_partition),&storage,0);if(!r)r=fs_mount(&mountp);result("FORMAT",r);return;}
 if(!strcmp(s,"fs ls")){struct fs_dir_t d;struct fs_dirent e;fs_dir_t_init(&d);r=fs_opendir(&d,"/lfs");while(!r&&!fs_readdir(&d,&e)&&e.name[0])printk("OK LS %s\n",e.name);fs_closedir(&d);result("LS",r);return;}
 if(!strcmp(s,"fs reboot")){printk("OK REBOOT\n");k_msleep(50);sys_reboot(SYS_REBOOT_COLD);return;}
 a=strchr(s,' ');if(!a){printk("INFO COMMANDS=fs format;fs ls;fs reboot;fs mkdir <path>;fs rmdir <path>;fs create <file>;fs write <file> <text>;fs read <file>;fs delete <file>\n");return;} *a++=0;
 if(!strcmp(s,"fs")&& !strncmp(a,"mkdir ",6)){r=fs_mkdir(a+6);result("MKDIR",r);return;} if(!strcmp(s,"fs")&& !strncmp(a,"rmdir ",6)){r=fs_unlink(a+6);result("RMDIR",r);return;} if(!strcmp(s,"fs")&& !strncmp(a,"delete ",7)){r=fs_unlink(a+7);result("DELETE",r);return;}
 if(!strcmp(s,"fs")&& !strncmp(a,"read ",5)){fs_file_t_init(&f);r=fs_open(&f,a+5,FS_O_READ);if(!r){r=fs_read(&f,b,127);b[r>0?r:0]=0;fs_close(&f);printk("OK READ DATA=%s\n",b);}else result("READ",r);return;}
 if(!strcmp(s,"fs")&&(!strncmp(a,"create ",7)||!strncmp(a,"write ",6))){data=a+(!strncmp(a,"create ",7)?7:6);char *v=strchr(data,' ');if(v)*v++=0;fs_file_t_init(&f);r=fs_open(&f,data,FS_O_CREATE|FS_O_RDWR);if(!r&&v){fs_seek(&f,0,FS_SEEK_SET);r=fs_write(&f,v,strlen(v));}fs_close(&f);result("WRITE",r);return;} result("UNSUPPORTED",-1);
}
int main(void){char b[160];size_t n=0;int r=fs_mount(&mountp);result("MOUNT",r);if(!device_is_ready(console_uart)){result("CONSOLE",-1);return 0;}for(;;){uint8_t c;if(uart_poll_in(console_uart,&c)){k_sleep(K_MSEC(10));continue;}if(c=='\r'||c=='\n'){if(n){b[n]=0;handle(b);n=0;}}else if(n<sizeof(b)-1)b[n++]=c;}}
