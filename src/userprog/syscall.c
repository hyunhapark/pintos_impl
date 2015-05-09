#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include "filesys/filesys.h"
#include <list.h>
#include <string.h>
#include "devices/shutdown.h"
#include "threads/palloc.h"
#include "userprog/process.h"

#define MIN(x, y)	(((x)>(y))?(y):(x))
#define MAX(x, y)	(((x)>(y))?(x):(y))

static void syscall_handler (struct intr_frame *);

static bool str_over_boundary (const char *);
static char *strlbond (char *, const char *, size_t);

/* Projects 2 and later. */
static void halt (void);
static void exit (int status);
static pid_t exec (const char *file);
static int wait (pid_t);
static bool create (const char *file, unsigned initial_size);
static bool remove (const char *file);
static int open (const char *file);
static int filesize (int fd);
static int read (int fd, void *buffer, unsigned length);
static int write (int fd, const void *buffer, unsigned length);
static void seek (int fd, unsigned position);
static unsigned tell (int fd);
static void close (int fd);

/* Project 3 and optionally project 4. */
static mapid_t mmap (int fd, void *addr);
static void munmap (mapid_t);

/* Project 4 only. */
static bool chdir (const char *dir);
static bool mkdir (const char *dir);
static bool readdir (int fd, char name[READDIR_MAX_LEN + 1]);
static bool isdir (int fd);
static int inumber (int fd);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f) 
{
	uint32_t *esp = f->esp;
	ASSERT (esp);
	if (esp>=PHYS_BASE){
		exit (-1);
	}
	esp = (uint32_t) user_vtop ((const void *) esp);
	if (esp==NULL){
		exit(-1);
	}
	int syscall_num = *esp;

	switch(syscall_num){
  /* Projects 2 and later. */
	case SYS_HALT:     /*printf("SYS_HALT\n"); */     halt (); break;
	case SYS_EXIT:     /*printf("SYS_EXIT\n"); */     exit ((int) *(esp+1)); break;
	case SYS_EXEC:     /*printf("SYS_EXEC\n"); */     f->eax = exec ((const char *) *(esp+1)); break;
	case SYS_WAIT:     /*printf("SYS_WAIT\n"); */     f->eax = wait ((pid_t) *(esp+1)); break;
	case SYS_CREATE:   /*printf("SYS_CREATE\n"); */   f->eax = create ((const char *) *(esp+1), (unsigned) *(esp+2)); break;
	case SYS_REMOVE:   /*printf("SYS_REMOVE\n"); */   f->eax = remove ((const char *) *(esp+1)); break;
	case SYS_OPEN:     /*printf("SYS_OPEN\n"); */     f->eax = open ((const char *) *(esp+1)); break;
	case SYS_FILESIZE: /*printf("SYS_FILESIZE\n"); */ f->eax = filesize ((int) *(esp+1)); break;
	case SYS_READ:     /*printf("SYS_READ\n"); */     f->eax = read ((int) *(esp+1), (void *) *(esp+2), (unsigned) *(esp+3)); break;
	case SYS_WRITE:    /*printf("SYS_WRITE\n"); */    write((int) *(esp+1), (const void *) *(esp+2), (unsigned) *(esp+3)); break;
	case SYS_SEEK:     /*printf("SYS_SEEK\n"); */     seek ((int) *(esp+1), (unsigned) *(esp+2)); break;
	case SYS_TELL:     /*printf("SYS_TELL\n"); */     f->eax = tell ((int) *(esp+1)); break;
	case SYS_CLOSE:    /*printf("SYS_CLOSE\n"); */    close ((int) *(esp+1)); break;

  /* Project 3 and optionally project 4. */
	case SYS_MMAP:     printf("SYS_MMAP\n"); break;
	case SYS_MUNMAP:   printf("SYS_MUNMAP\n"); break;

  /* Project 4 only. */
	case SYS_CHDIR:    printf("SYS_CHDIR\n"); break;
	case SYS_MKDIR:    printf("SYS_MKDIR\n"); break;
	case SYS_READDIR:  printf("SYS_READDIR\n"); break;
	case SYS_ISDIR:    printf("SYS_ISDIR\n"); break;
	case SYS_INUMBER:  printf("SYS_INUMBER\n"); break;
	default:	PANIC ("Wrong System call.\n"); break;
	}
}

static bool
str_over_boundary (const char *s){
	char *page = (char *) pg_round_down (s);
	uintptr_t offset = pg_ofs (s);

	for (; offset<PGSIZE; offset++)
		if(page[offset]=='\0')
			return false;
	return true;
}


/* Bond and copy string from user virtual address src to kernel 
	 page dst. It cannot copy string over one page. */
static char *
strlbond (char *dst, const char *src, size_t size){
	
	const char *s = (const char *) user_vtop ((const void *) src);
	uintptr_t soffset = pg_ofs(src);
	uintptr_t doffset = 0;
	if (str_over_boundary (s)){
		memcpy (dst, s, MIN(PGSIZE-soffset, size-1));
		doffset += MIN(PGSIZE-soffset, size-1);
		dst[doffset] = '\0';
		s = pg_round_down (src + PGSIZE);
	}

	ASSERT (!str_over_boundary (s));

	strlcpy (dst+doffset, s, size-doffset);

	return dst;
}

static void
halt (void) 
{
  shutdown_power_off ();
}

static void
exit (int status)
{
	struct thread *cur = thread_current ();
	cur->exit_status = status;
	printf ("%s: exit(%d)\n", cur->name, status);
	thread_exit ();
}

static pid_t
exec (const char *_cmd_line)
{
	if (_cmd_line >= PHYS_BASE) {
		return -1;
	}
	char *cmd_line = (char *) palloc_get_page (0);
	strlbond (cmd_line, _cmd_line, PGSIZE);
	pid_t pid = (pid_t) process_execute (cmd_line);
	palloc_free_page (cmd_line);
	
	/* Wait for load. */
	struct thread *child = get_thread_by_tid ((tid_t) pid);
	ASSERT (child);
	sema_down (&child->loaded);

	return pid;
}

static int
wait (pid_t pid)
{
	return process_wait((tid_t) pid);
}

static bool
create (const char *_file, unsigned initial_size)
{
	if (_file >= PHYS_BASE) {
		return false;
	}
	bool success = false;
	char *file = (char *) palloc_get_page (0);
	strlbond (file, _file, PGSIZE);
	success = filesys_create (file, initial_size);
	palloc_free_page (file);
  return success;
}

static bool
remove (const char *_file)
{
	if (_file >= PHYS_BASE) {
		return false;
	}
	bool success = false;
	char *file = (char *) palloc_get_page (0);
	strlbond (file, _file, PGSIZE);
	success = filesys_remove (file);
	palloc_free_page (file);
  return success;
}

static int
open (const char *_file)
{
	if (_file >= PHYS_BASE) {
		return -1;
	}
	struct thread *t = thread_current ();
	struct file *f;
	int fd = -1;

	char *file = (char *) palloc_get_page (0);
	strlbond (file, _file, PGSIZE);
	f = filesys_open (file);
	palloc_free_page (file);
	if (f==NULL) { // file open fail.
		return -1;
	}
	fd = (++t->lastfd);

	// Store (fd, f) into thread's open_list
	struct openfile *of = (struct openfile *) calloc (1, sizeof(struct openfile));
	if (of==NULL) {	// store mapping info fail.
		--t->lastfd;
		return -1;
	}
	of->fd = fd;
	of->f = f;
	list_push_back (&t->open_list, &of->openelem);

  return fd;
}

static int
filesize (int fd) 
{
	//TODO
  return 0;
}

static int
read (int fd, void *_buffer, unsigned size)
{
	if (_buffer >= PHYS_BASE) {
		return -1;
	}
	//TODO
  return 0;
}

static int
write (int fd, const void *_buffer, unsigned size)
{
	if (_buffer >= PHYS_BASE) {
		return -1;
	}
	int wrote = 0;

	char *buffer = (char *) palloc_get_page (0);
	while (size>0){
		strlbond (buffer, _buffer, MIN(size+1, PGSIZE));

		if (fd == STDOUT_FILENO){
			putbuf (buffer, MIN(size, PGSIZE-1));
			wrote += MIN(size, PGSIZE-1);
			size -= MIN(size, PGSIZE-1);
		} else {
			//TODO
			//putbuf (buffer, MIN(size, PGSIZE-1));
			wrote += MIN(size, PGSIZE-1);
			size -= MIN(size, PGSIZE-1);
		}
	}
	palloc_free_page (buffer);
  return wrote;
}

static void
seek (int fd, unsigned position) 
{
	//TODO
}

static unsigned
tell (int fd) 
{
	//TODO
	return 0;
}

static void
close (int fd)
{
	//TODO
}

/* ----- til here, enough for project2 ----- */

static mapid_t
mmap (int fd, void *_addr)
{
	//TODO
  return 0;
}

static void
munmap (mapid_t mapid)
{
  //TODO
}

/* ----- til here, enough for project3 ----- */

static bool
chdir (const char *_dir)
{
	//TODO
  return false;
}

static bool
mkdir (const char *_dir)
{
	//TODO
  return false;
}

static bool
readdir (int fd, char name[READDIR_MAX_LEN + 1]) 
{
	//TODO
  return false;
}

static bool
isdir (int fd) 
{
	//TODO
  return false;
}

static int
inumber (int fd) 
{
	//TODO
  return 0;
}
