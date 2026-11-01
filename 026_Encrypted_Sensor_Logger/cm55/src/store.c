/* Educational streaming AEAD. Filenames/lengths remain public. UART is trusted.
 * No export permission on the persistent key. No guarantee against replay of
 * a complete old file, physical debug access, or malicious file deletion.
 */
#include "store.h"
#include <errno.h>
#include <string.h>
#include <psa/crypto.h>
#include <psa/protected_storage.h>
#include <zephyr/fs/fs.h>
#include <zephyr/fs/littlefs.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#define KEY_ID 0x2601
#define COUNTER_UID 0x534c4701ULL
#define HEADER 64
#define TAG 16
#define PAYLOAD (SAMPLE_BYTES * BATCH_SAMPLES)
#define NONCE_RESERVATION 64
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(lfs);
static struct fs_mount_t mountp = {
    .type=FS_LITTLEFS, .fs_data=&lfs,
    .storage_dev=(void *)PARTITION_ID(logger_partition),
    .mnt_point="/log", .flags=FS_MOUNT_FLAG_NO_FORMAT,
};
static const struct flash_area *area;
static struct store_status s;
static struct fs_file_t output;
static uint64_t reserved;
static uint8_t previous[TAG];
static uint8_t record[HEADER + PAYLOAD + TAG], plain[PAYLOAD], aad[HEADER + LOG_NAME_MAX];

static void wipe(void *p, size_t n)
{ volatile uint8_t *v=p; while(n--) *v++=0; }
static void hex_out(const uint8_t *p, size_t n)
{ while(n--) printk("%02x",*p++); }
bool store_name_valid(const char *name)
{
    if(!name || !*name || strlen(name)>LOG_NAME_MAX) return false;
    for(const char *p=name;*p;p++)
        if(!((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||
             (*p>='0'&&*p<='9')||*p=='_'||*p=='-')) return false;
    return true;
}
static void path(char out[32], const char *name)
{ snprintk(out,32,"/log/%s",name); }
static void attr_set(psa_key_attributes_t *attr, bool persistent)
{
    psa_set_key_type(attr,PSA_KEY_TYPE_AES);
    psa_set_key_bits(attr,256);
    psa_set_key_algorithm(attr,PSA_ALG_GCM);
    psa_set_key_usage_flags(attr,PSA_KEY_USAGE_ENCRYPT|PSA_KEY_USAGE_DECRYPT);
    if(persistent) {
        psa_set_key_lifetime(attr,PSA_KEY_LIFETIME_PERSISTENT);
        psa_set_key_id(attr,KEY_ID);
    }
}
static int key_init(void)
{
    psa_key_attributes_t attr=PSA_KEY_ATTRIBUTES_INIT;
    uint8_t state[8]; size_t length=0;
    psa_status_t ks=psa_get_key_attributes(KEY_ID,&attr);
    psa_status_t cs=psa_ps_get(COUNTER_UID,0,sizeof(state),state,&length);
    printk("DETAIL LOGGER KEY=%d COUNTER=%d length=%zu\n",ks,cs,length);
    if(ks==PSA_SUCCESS) {
        bool valid=psa_get_key_type(&attr)==PSA_KEY_TYPE_AES &&
            psa_get_key_bits(&attr)==256 && psa_get_key_algorithm(&attr)==PSA_ALG_GCM &&
            psa_get_key_usage_flags(&attr)==(PSA_KEY_USAGE_ENCRYPT|PSA_KEY_USAGE_DECRYPT);
        psa_reset_key_attributes(&attr);
        if(!valid || cs!=PSA_SUCCESS || length!=8) return -EKEYREJECTED;
        s.nonce=reserved=sys_get_le64(state); /* skip all unused reservations on boot */
        return 0;
    }
    psa_reset_key_attributes(&attr);
    if((ks!=PSA_ERROR_DOES_NOT_EXIST && ks!=PSA_ERROR_INVALID_HANDLE)||
        cs!=PSA_ERROR_DOES_NOT_EXIST) return -EKEYREJECTED;
    memset(state,0,sizeof(state));
    if(psa_ps_set(COUNTER_UID,8,state,PSA_STORAGE_FLAG_NONE)!=PSA_SUCCESS) return -EIO;
    attr_set(&attr,true); psa_key_id_t id;
    ks=psa_generate_key(&attr,&id); psa_reset_key_attributes(&attr);
    return ks==PSA_SUCCESS ? 0 : -EIO;
}
static int nonce_next(uint8_t nonce[12])
{
    if(!s.key_ready) return -EKEYREJECTED;
    if(s.nonce==reserved) {
        if(reserved>UINT64_MAX-NONCE_RESERVATION) return -EOVERFLOW;
        uint8_t state[8]; uint64_t next=reserved+NONCE_RESERVATION;
        sys_put_le64(next,state);
        if(psa_ps_set(COUNTER_UID,8,state,PSA_STORAGE_FLAG_NONE)!=PSA_SUCCESS) {
            s.key_ready=false; return -EIO;
        }
        reserved=next;
    }
    sys_put_le32(KEY_ID,nonce); sys_put_le64(++s.nonce,nonce+4);
    return 0;
}
static void space_update(void)
{
    struct fs_statvfs v;
    if(s.mounted && !fs_statvfs("/log",&v)) s.free_bytes=v.f_bfree*v.f_frsize;
}
void store_snapshot(struct store_status *out) { *out=s; }
int store_init(void)
{
    int ret=flash_area_open(PARTITION_ID(logger_partition),&area);
    if(ret) return ret;
    struct flash_pages_info page={0};
    ret=flash_get_page_info_by_offs(area->fa_dev,area->fa_off,&page);
    printk("DETAIL LOGGER FLASH offset=%lx size=%lx sector=%lx rc=%d\n",
        (unsigned long)area->fa_off,(unsigned long)area->fa_size,(unsigned long)page.size,ret);
    if(ret || area->fa_off!=0x1780000 || area->fa_size!=0x300000 || page.size!=0x40000)
        return -EINVAL;
    ret=psa_crypto_init(); if(ret) return -EIO;
    ret=key_init(); if(ret) return ret;
    s.key_ready=true;
    ret=fs_mount(&mountp);
    if(!ret) s.mounted=true;
    space_update();
    return ret;
}
int store_init_blank(void)
{
    if(s.mounted) return 0;
    if(!area || !s.key_ready) return -ENODEV;
    uint8_t bytes[256];
    for(off_t offset=0;offset<area->fa_size;offset+=sizeof(bytes)) {
        int ret=flash_area_read(area,offset,bytes,sizeof(bytes)); if(ret) return ret;
        for(size_t i=0;i<sizeof(bytes);i++) if(bytes[i]!=0xff) {
            printk("DETAIL INIT NONBLANK flash_offset=%lx; no erase performed\n",
                   (unsigned long)(area->fa_off+offset+i));
            return -EEXIST;
        }
    }
    int ret=fs_mkfs(FS_LITTLEFS,(uintptr_t)PARTITION_ID(logger_partition),NULL,0);
    if(!ret) ret=fs_mount(&mountp);
    if(!ret) s.mounted=true;
    space_update(); return ret;
}
static void aad_make(const char *name)
{
    memcpy(aad,record,HEADER); memset(aad+HEADER,0,LOG_NAME_MAX);
    memcpy(aad+HEADER,name,strlen(name));
}
int store_start(const char *name)
{
    if(!s.mounted || !s.key_ready) return -ENODEV;
    if(s.opened) return -EBUSY;
    if(!store_name_valid(name)) return -EINVAL;
    char file[32]; path(file,name); struct fs_dirent entry;
    int ret=fs_stat(file,&entry);
    if(!ret) return -EEXIST;
    if(ret!=-ENOENT) return ret;
    uint8_t nonce[12]; ret=nonce_next(nonce); if(ret) return ret;
    fs_file_t_init(&output);
    ret=fs_open(&output,file,FS_O_CREATE|FS_O_WRITE); if(ret) return ret;
    s.opened=true; s.samples=s.batches=s.bytes=0;
    s.session=sys_get_le64(nonce+4); strcpy(s.name,name); memset(previous,0,TAG);
    return 0;
}
static int append_record(const uint8_t *data, unsigned count, bool seal)
{
    if(!s.opened) return -EBADF;
    unsigned length=seal?8:count*SAMPLE_BYTES;
    if(!seal && (!count || count>BATCH_SAMPLES)) return -EINVAL;
    memset(record,0,HEADER); memcpy(record,"SLG1",4);
    record[4]=1; record[5]=seal?2:1;
    sys_put_le16(count,record+6); sys_put_le32(length,record+8);
    sys_put_le32(s.batches,record+12); sys_put_le64(s.session,record+16);
    int ret=nonce_next(record+24); if(ret) return ret;
    sys_put_le32(KEY_ID,record+36); memcpy(record+40,previous,TAG);
    aad_make(s.name); size_t encrypted=0;
    psa_status_t status=psa_aead_encrypt(KEY_ID,PSA_ALG_GCM,record+24,12,
        aad,sizeof(aad),data,length,record+HEADER,PAYLOAD+TAG,&encrypted);
    if(status!=PSA_SUCCESS || encrypted!=length+TAG) return -EIO;
    ssize_t written=fs_write(&output,record,HEADER+encrypted);
    if(written!=(ssize_t)(HEADER+encrypted)) return written<0?written:-EIO;
    ret=fs_sync(&output); if(ret) return ret;
    memcpy(previous,record+HEADER+length,TAG); s.bytes+=HEADER+encrypted;
    if(!seal) { s.batches++; s.samples+=count; }
    space_update(); return 0;
}
int store_append(const uint8_t *samples, unsigned count)
{ return append_record(samples,count,false); }
void store_abort(void)
{
    if(s.opened) fs_close(&output);
    s.opened=false; wipe(plain,sizeof(plain)); space_update();
}
int store_stop(void)
{
    uint8_t totals[8]; sys_put_le32(s.samples,totals); sys_put_le32(s.batches,totals+4);
    int ret=append_record(totals,0,true);
    int closed=s.opened?fs_close(&output):0; s.opened=false; space_update();
    return ret?ret:closed;
}

/* Returns 1 for a whole record, 0 for EOF, 2 for a torn final record. All
 * full records must authenticate. Never mistake an invalid tag for a reset.
 */
static int record_read(struct fs_file_t *f, unsigned *length)
{
    ssize_t n=fs_read(f,record,HEADER);
    if(n<0) return n;
    if(!n) return 0;
    if(n!=HEADER) return 2;
    if(memcmp(record,"SLG1",4)||record[4]!=1||
       (record[5]!=1 && record[5]!=2)||sys_get_le32(record+36)!=KEY_ID ||
       memcmp(record+24,record+36,4)) return -EBADMSG;
    for(unsigned i=56;i<64;i++) if(record[i]) return -EBADMSG;
    *length=sys_get_le32(record+8); unsigned count=sys_get_le16(record+6);
    if((record[5]==1 && (!count||count>BATCH_SAMPLES||*length!=count*SAMPLE_BYTES)) ||
       (record[5]==2 && (count||*length!=8))) return -EBADMSG;
    n=fs_read(f,record+HEADER,*length+TAG);
    if(n<0) return n;
    return n==*length+TAG?1:2;
}
static int decrypt_record(const char *name, unsigned length)
{
    aad_make(name); size_t n=0;
    psa_status_t ret=psa_aead_decrypt(KEY_ID,PSA_ALG_GCM,record+24,12,
        aad,sizeof(aad),record+HEADER,length+TAG,plain,sizeof(plain),&n);
    if(ret!=PSA_SUCCESS || n!=length) { wipe(plain,sizeof(plain)); return -EBADMSG; }
    return 0;
}
static int scan(const char *name, bool emit, bool report)
{
    char file[32]; path(file,name); struct fs_file_t f; fs_file_t_init(&f);
    int ret=fs_open(&f,file,FS_O_READ); if(ret) return ret;
    uint32_t batches=0,samples=0; uint64_t session=0,last_nonce=0;
    uint8_t prev[TAG]={0}; bool sealed=false,torn=false;
    for(;;) {
        unsigned length=0; ret=record_read(&f,&length);
        if(ret==0) break;
        if(sealed) { ret=-EBADMSG; break; }
        if(ret==2) { torn=true; ret=0; break; }
        if(ret<0) break;
        ret=decrypt_record(name,length); if(ret) break;
        uint64_t id=sys_get_le64(record+16), nonce=sys_get_le64(record+28);
        if(!batches) session=id;
        if(!session || id!=session || nonce<=last_nonce || nonce<=session ||
           sys_get_le32(record+12)!=batches || memcmp(prev,record+40,TAG)) {
            ret=-EBADMSG; break;
        }
        last_nonce=nonce; memcpy(prev,record+HEADER+length,TAG);
        if(record[5]==2) {
            if(sys_get_le32(plain)!=samples || sys_get_le32(plain+4)!=batches) {
                ret=-EBADMSG; break;
            }
            sealed=true;
        } else {
            unsigned count=sys_get_le16(record+6);
            for(unsigned i=0;emit && i<count;i++) {
                const uint8_t *p=plain+i*SAMPLE_BYTES;
                printk("ROW %llu",(unsigned long long)sys_get_le64(p));
                for(unsigned j=8;j<SAMPLE_BYTES;j+=4) printk(",%d",(int32_t)sys_get_le32(p+j));
                printk("\n");
            }
            samples+=count; batches++;
        }
        wipe(plain,sizeof(plain));
    }
    wipe(plain,sizeof(plain)); int closed=fs_close(&f);
    if(ret>=0) ret=closed;
    if(!ret && report) printk("OK READ name=%s samples=%u batches=%u sealed=%u torn=%u session=%llu\n",
        name,samples,batches,sealed,torn,(unsigned long long)session);
    return ret;
}
int store_read(const char *name, bool emit)
{
    if(!store_name_valid(name)) return -EINVAL;
    if(s.opened) return -EBUSY;
    /* No plaintext is released until the entire visible file passes preflight.
     * Single storage owner means file cannot change between these two passes. */
    int ret=scan(name,false,false);
    return ret?ret:scan(name,emit,true);
}
int store_delete(const char *name)
{
    if(!store_name_valid(name)) return -EINVAL;
    if(s.opened) return -EBUSY;
    char file[32]; path(file,name); int ret=fs_unlink(file); space_update(); return ret;
}
int store_list(void)
{
    if(!s.mounted) return -ENODEV;
    struct fs_dir_t dir; fs_dir_t_init(&dir); int ret=fs_opendir(&dir,"/log"); if(ret) return ret;
    struct fs_dirent entry; unsigned count=0;
    while(!(ret=fs_readdir(&dir,&entry)) && entry.name[0]) {
        printk("FILE name=%s bytes=%zu\n",entry.name,entry.size); count++;
    }
    int close_ret=fs_closedir(&dir); if(!ret) ret=close_ret;
    if(!ret) printk("OK LIST count=%u\n",count);
    return ret;
}
static int locate(struct fs_file_t *f, const char *name, unsigned index, unsigned *length)
{
    if(!store_name_valid(name)||index>100000) return -EINVAL;
    if(s.opened) return -EBUSY;
    char file[32]; path(file,name); fs_file_t_init(f);
    int ret=fs_open(f,file,FS_O_READ|FS_O_WRITE); if(ret) return ret;
    for(unsigned i=0;i<=index;i++) {
        ret=record_read(f,length);
        if(ret!=1) { fs_close(f); return ret<0?ret:-ENOENT; }
    }
    return 0;
}
int store_raw(const char *name, unsigned index)
{
    struct fs_file_t f; unsigned length; int ret=locate(&f,name,index,&length); if(ret) return ret;
    ret=fs_close(&f);
    if(!ret) { printk("OK RAW hex="); hex_out(record,HEADER+length+TAG); printk("\n"); }
    return ret;
}
int store_tamper(const char *name, unsigned index, const char *part)
{
    if(strcmp(part,"header")&&strcmp(part,"data")&&strcmp(part,"tag")) return -EINVAL;
    struct fs_file_t f; unsigned length; int ret=locate(&f,name,index,&length); if(ret) return ret;
    unsigned pos=!strcmp(part,"header")?16:!strcmp(part,"data")?HEADER:HEADER+length;
    off_t start=fs_tell(&f)-(HEADER+length+TAG);
    record[pos]^=1;
    ret=fs_seek(&f,start+pos,FS_SEEK_SET);
    if(!ret && fs_write(&f,record+pos,1)!=1) ret=-EIO;
    if(!ret) ret=fs_sync(&f);
    int closed=fs_close(&f); return ret?ret:closed;
}
int store_kat(void)
{
    uint8_t key[32],nonce[12],input[32],cipher[48],decoded[32];
    /* CM33's secure service must receive RAM-backed inputs, not M55 XIP
     * aliases that are outside the CM33 non-secure caller memory window. */
    uint8_t kat_aad[10]="SLG-KAT-v1";
    for(unsigned i=0;i<32;i++) { key[i]=i; input[i]=0x80+i; }
    for(unsigned i=0;i<12;i++) nonce[i]=i;
    psa_key_attributes_t attr=PSA_KEY_ATTRIBUTES_INIT; attr_set(&attr,false);
    psa_key_id_t id; psa_status_t ret=psa_import_key(&attr,key,32,&id);
    psa_reset_key_attributes(&attr); wipe(key,32); if(ret) return -EIO;
    size_t n=0;
    ret=psa_aead_encrypt(id,PSA_ALG_GCM,nonce,12,kat_aad,sizeof(kat_aad),
        input,32,cipher,48,&n);
    if(!ret) { printk("DETAIL KAT hex="); hex_out(cipher,n); printk("\n"); }
    if(!ret) ret=psa_aead_decrypt(id,PSA_ALG_GCM,nonce,12,kat_aad,sizeof(kat_aad),
        cipher,n,decoded,32,&n);
    if(!ret && (n!=32 || memcmp(input,decoded,32))) ret=PSA_ERROR_CORRUPTION_DETECTED;
    psa_status_t destroyed=psa_destroy_key(id); wipe(decoded,32);
    if(ret || destroyed) return -EIO;
    /* A different volatile key must reject the same authentic ciphertext. */
    memset(key,0xff,sizeof(key)); attr_set(&attr,false);
    ret=psa_import_key(&attr,key,sizeof(key),&id);
    psa_reset_key_attributes(&attr); wipe(key,sizeof(key));
    if(ret) return -EIO;
    ret=psa_aead_decrypt(id,PSA_ALG_GCM,nonce,12,kat_aad,sizeof(kat_aad),
        cipher,sizeof(cipher),decoded,sizeof(decoded),&n);
    destroyed=psa_destroy_key(id); wipe(decoded,sizeof(decoded));
    if(ret!=PSA_ERROR_INVALID_SIGNATURE || destroyed) return -EIO;
    ret=psa_export_key(KEY_ID,key,sizeof(key),&n); wipe(key,sizeof(key));
    if(ret!=PSA_ERROR_NOT_PERMITTED) return -EACCES;
    printk("DETAIL KAT wrong_key_rejected=1 persistent_key_export_denied=1\n");
    return 0;
}
