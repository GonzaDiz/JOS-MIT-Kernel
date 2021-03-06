/* See COPYRIGHT for copyright information. */

#include <inc/x86.h>
#include <inc/error.h>
#include <inc/string.h>
#include <inc/assert.h>

#include <kern/env.h>
#include <kern/pmap.h>
#include <kern/trap.h>
#include <kern/syscall.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <kern/e1000.h>

// Funcion propia para validar direcciones
// de memoria virtual pasadas a la syscall
static int
check_va(const void * va) {
	uintptr_t casted = (uintptr_t)va;
	if (PGOFF(casted) || casted >= UTOP) return -E_INVAL;
	else return 0;
}

// Funcion propia para validar los permisos
// Primero comprobamos que no tengan permisos invalidos
// Segundo comprobamos que los mandatorios esten seteados
static int
check_permissions(int perm) {
	if (perm & ~PTE_SYSCALL || ((perm & (PTE_U | PTE_P)) != (PTE_U | PTE_P))) return -E_INVAL;
	else return 0;
}

// Print a string to the system console.
// The string is exactly 'len' characters long.
// Destroys the environment on memory errors.
static void
sys_cputs(const char *s, size_t len)
{
	// Check that the user has permission to read memory [s, s+len).
	// Destroy the environment if not.

	// LAB 3: Your code here.
	user_mem_assert(curenv, (const void *) s, len, PTE_U);
	// Print the string supplied by the user.
	cprintf("%.*s", len, s);
}

// Read a character from the system console without blocking.
// Returns the character, or 0 if there is no input waiting.
static int
sys_cgetc(void)
{
	return cons_getc();
}

// Returns the current environment's envid.
static envid_t
sys_getenvid(void)
{
	return curenv->env_id;
}

// Destroy a given environment (possibly the currently running environment).
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_destroy(envid_t envid)
{
	int r;
	struct Env *e;

	if ((r = envid2env(envid, &e, 1)) < 0)
		return r;
	if (e == curenv)
		cprintf("[%08x] exiting gracefully\n", curenv->env_id);
	else
		cprintf("[%08x] destroying %08x\n", curenv->env_id, e->env_id);
	env_destroy(e);
	return 0;
}

// Deschedule current environment and pick a different one to run.
static void
sys_yield(void)
{
	sched_yield();
}

// Allocate a new environment.
// Returns envid of new environment, or < 0 on error.  Errors are:
//	-E_NO_FREE_ENV if no free environment is available.
//	-E_NO_MEM on memory exhaustion.
static envid_t
sys_exofork(void)
{
	// Create the new environment with env_alloc(), from kern/env.c.
	// It should be left as env_alloc created it, except that
	// status is set to ENV_NOT_RUNNABLE, and the register set is copied
	// from the current environment -- but tweaked so sys_exofork
	// will appear to return 0.

	// LAB 4: Your code here.
	struct Env * new_env;
	// Creamos un nuevo proceso y el padre sera el proceso actual
	int ret = env_alloc(&new_env, curenv->env_id);
	// Comprobamos errores de env_alloc
	if (ret < 0) return (envid_t)ret;
	// Actualizamos el estado del nuevo proceso
	new_env->env_status = ENV_NOT_RUNNABLE;
	// Copiamos el trap frame del padre
	memcpy(&new_env->env_tf, &curenv->env_tf, sizeof(struct Trapframe));
	// Forzamos 0 en EAX del hijo asi quien llama a exofork
	// puede diferenciar entre hijo y padre (pues en EAX esta
	// lo que devuelve la syscall)
	new_env->env_tf.tf_regs.reg_eax = 0;
	// Retornamos el env_id del proceso creado
	return new_env->env_id;
}

// Set envid's env_status to status, which must be ENV_RUNNABLE
// or ENV_NOT_RUNNABLE.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if status is not a valid status for an environment.
static int
sys_env_set_status(envid_t envid, int status)
{
	// Hint: Use the 'envid2env' function from kern/env.c to translate an
	// envid to a struct Env.
	// You should set envid2env's third argument to 1, which will
	// check whether the current environment has permission to set
	// envid's status.

	// LAB 4: Your code here.
	struct Env * env;

	// Pasamos envid a struct Env
	// Ponemos el checkeo de permisos en true
	// que comprueba que el envid pasado
	// corresponde a currenv o a un hijo 
	// inmediato de currenv.
	int ret = envid2env(envid, &env, true);

	// Comprobamos errores

	if (ret < 0) return ret;

	// Comprobamos status a setear validos
	if (status != ENV_RUNNABLE && status != ENV_NOT_RUNNABLE) return -E_INVAL;

	// Si pasamos las validaciones seteamos el status y retornamos
	env->env_status = status;

	return 0;
}

// Set envid's trap frame to 'tf'.
// tf is modified to make sure that user environments always run at code
// protection level 3 (CPL 3) with interrupts enabled.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_trapframe(envid_t envid, struct Trapframe *tf)
{
	// LAB 5: Your code here.
	// Remember to check whether the user has supplied us with a good
	// address!
	// Definimos el struct Env
	struct Env * env;

	// Comprobamos que la direccion del trapframe
	if ((uintptr_t) tf >= UTOP) return -E_INVAL;

	// Pasamos de envid a struct Env
	envid2env(envid, &env, true);

	// Validamos que el proceso exista o el caller tenga permisos
	if (env == NULL) return -E_BAD_ENV;

	// Copiamos el TrapFrame en el struct Env
	memcpy(&env->env_tf, tf, sizeof(struct Trapframe));

	// Nos aseguramos que el usuario siempre code en nivel
	// de proteccion 3 (CPL 3)
	env->env_tf.tf_cs = GD_UT | 3;

	// Nos aseguramos que tendra las interrupciones habilitadas
	env->env_tf.tf_eflags |= FL_IF;

	// Y ademas IOPL a 0
	env->env_tf.tf_eflags |= FL_IOPL_0;

	return 0;
}

// Set the page fault upcall for 'envid' by modifying the corresponding struct
// Env's 'env_pgfault_upcall' field.  When 'envid' causes a page fault, the
// kernel will push a fault record onto the exception stack, then branch to
// 'func'.
//
// Returns 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
static int
sys_env_set_pgfault_upcall(envid_t envid, void *func)
{
	// LAB 4: Your code here.

	//Chequeamos errores
	struct Env * env;
	if (envid2env(envid, &env, true) < 0) {
		return -E_BAD_ENV;
	}

	// Seteamos el handler del usuario para un pgfault del proceso envid
	env->env_pgfault_upcall = func;

	return 0;
}

// Allocate a page of memory and map it at 'va' with permission
// 'perm' in the address space of 'envid'.
// The page's contents are set to 0.
// If a page is already mapped at 'va', that page is unmapped as a
// side effect.
//
// perm -- PTE_U | PTE_P must be set, PTE_AVAIL | PTE_W may or may not be set,
//         but no other bits may be set.  See PTE_SYSCALL in inc/mmu.h.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
//	-E_INVAL if perm is inappropriate (see above).
//	-E_NO_MEM if there's no memory to allocate the new page,
//		or to allocate any necessary page tables.
static int
sys_page_alloc(envid_t envid, void *va, int perm)
{
	// Hint: This function is a wrapper around page_alloc() and
	//   page_insert() from kern/pmap.c.
	//   Most of the new code you write should be to check the
	//   parameters for correctness.
	//   If page_insert() fails, remember to free the page you
	//   allocated!

	// LAB 4: Your code here.
	// Variables
	struct Env * env;
	struct PageInfo * page;

	// Pasamos envid a struct Env
	// Y comprobamos de igual manera 
	// que en sys_env_set_status
	int error = envid2env(envid, &env, true);

	// Comprobamos errores
	if (error) return error; // -E_BAD_ENV

	// Validamos va
	error = check_va(va);
	if (error) return error;
	
	// Validamos permisos
	error = check_permissions(perm);
	if (error) return error;

	// Alocamos la pagina y validamos que haya memoria
	page = page_alloc(ALLOC_ZERO);
	if (page == NULL) return -E_NO_MEM;

	// Mapeamos la pagina y validamos errores
	error = page_insert(env->env_pgdir, page, va, perm);
	if (error) {
		page_free(page); // Liberamos la pagina (nunca se incremento pp_ref)
		return error; 	 // -E_NO_MEM
	}

	// Success
	return 0;
}

// Map the page of memory at 'srcva' in srcenvid's address space
// at 'dstva' in dstenvid's address space with permission 'perm'.
// Perm has the same restrictions as in sys_page_alloc, except
// that it also must not grant write access to a read-only
// page.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if srcenvid and/or dstenvid doesn't currently exist,
//		or the caller doesn't have permission to change one of them.
//	-E_INVAL if srcva >= UTOP or srcva is not page-aligned,
//		or dstva >= UTOP or dstva is not page-aligned.
//	-E_INVAL is srcva is not mapped in srcenvid's address space.
//	-E_INVAL if perm is inappropriate (see sys_page_alloc).
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in srcenvid's
//		address space.
//	-E_NO_MEM if there's no memory to allocate any necessary page tables.
static int
sys_page_map(envid_t srcenvid, void *srcva, envid_t dstenvid, void *dstva, int perm)
{
	// Hint: This function is a wrapper around page_lookup() and
	//   page_insert() from kern/pmap.c.
	//   Again, most of the new code you write should be to check the
	//   parameters for correctness.
	//   Use the third argument to page_lookup() to
	//   check the current permissions on the page.

	// LAB 4: Your code here.
	// Variables
	struct Env *src_env, *dst_env;
	struct PageInfo * page;
	pte_t * pte;
	int error = 0;

	// Pasamos envid's a struct Env's
	// Y comprobamos de igual manera 
	// que en sys_env_set_status
	error = envid2env(srcenvid, &src_env, true);
	if (error) return error;
	error = envid2env(dstenvid, &dst_env, true);
	if (error) return error;

	// Validamos va's
	error = check_va(srcva);
	if (error) return error;
	error = check_va(dstva);
	if (error) return error;

	// Validamos que srcva este mapeado en src_env Address Space
	page = page_lookup(src_env->env_pgdir, srcva, &pte);
	if (page == NULL) return -E_INVAL;

	// Validamos permisos
	// Idem page alloc
	// Validamos permisos
	error = check_permissions(perm);
	if (error) return error;

	// Verificamos que no sea una pagina de solo lectura
	// y se la este intentando mapear con permisos de escritura
	if (((*pte & PTE_W) == 0) && (perm & PTE_W)) return -E_INVAL;

	// Mapeamos la pagina y validamos errores
	error = page_insert(dst_env->env_pgdir, page, dstva, perm);
	if (error) return error; 	 // -E_NO_MEM
	
	return 0;
}

// Unmap the page of memory at 'va' in the address space of 'envid'.
// If no page is mapped, the function silently succeeds.
//
// Return 0 on success, < 0 on error.  Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist,
//		or the caller doesn't have permission to change envid.
//	-E_INVAL if va >= UTOP, or va is not page-aligned.
static int
sys_page_unmap(envid_t envid, void *va)
{
	// Hint: This function is a wrapper around page_remove().

	// LAB 4: Your code here.
	// Variables
	struct Env * env;
	struct PageInfo * page;
	int error = 0;
	error = envid2env(envid, &env, true);
	if (error) return error;

	// Validamos va
	error = check_va(va);
	if (error) return error;

	// Validamos que haya pagina mapeada sino
	// page_remove podria fallar en page_decref.
	page = page_lookup(env->env_pgdir, va, NULL);
	if (page == NULL) return 0; // "silently succeeds"

	// Removemos la pagina
	page_remove(env->env_pgdir, va);

	return 0;
}

// Try to send 'value' to the target env 'envid'.
// If srcva < UTOP, then also send page currently mapped at 'srcva',
// so that receiver gets a duplicate mapping of the same page.
//
// The send fails with a return value of -E_IPC_NOT_RECV if the
// target is not blocked, waiting for an IPC.
//
// The send also can fail for the other reasons listed below.
//
// Otherwise, the send succeeds, and the target's ipc fields are
// updated as follows:
//    env_ipc_recving is set to 0 to block future sends;
//    env_ipc_from is set to the sending envid;
//    env_ipc_value is set to the 'value' parameter;
//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
// The target environment is marked runnable again, returning 0
// from the paused sys_ipc_recv system call.  (Hint: does the
// sys_ipc_recv function ever actually return?)
//
// If the sender wants to send a page but the receiver isn't asking for one,
// then no page mapping is transferred, but no error occurs.
// The ipc only happens when no errors occur.
//
// Returns 0 on success, < 0 on error.
// Errors are:
//	-E_BAD_ENV if environment envid doesn't currently exist.
//		(No need to check permissions.)
//	-E_IPC_NOT_RECV if envid is not currently blocked in sys_ipc_recv,
//		or another environment managed to send first.
//	-E_INVAL if srcva < UTOP but srcva is not page-aligned.
//	-E_INVAL if srcva < UTOP and perm is inappropriate
//		(see sys_page_alloc).
//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
//		address space.
//	-E_INVAL if (perm & PTE_W), but srcva is read-only in the
//		current environment's address space.
//	-E_NO_MEM if there's not enough memory to map srcva in envid's
//		address space.
static int
sys_ipc_try_send(envid_t envid, uint32_t value, void *srcva, unsigned perm)
{
	// LAB 4: Your code here.
	struct Env * to_env;
	bool trans_page = false;
	int ret = envid2env(envid, &to_env, false);
	if (ret < 0) {
		// return -E_BAD_ENV if environment envid doesn't currently exist
		return -E_BAD_ENV;
	}

	// The send fails with a return value of -E_IPC_NOT_RECV if the
	// target is not blocked, waiting for an IPC.
	if (!to_env->env_ipc_recving) {
		return -E_IPC_NOT_RECV;
	}

	// Mapeamos si AMBOS procesos lo solicitaro
	if ((uintptr_t)srcva < UTOP && (uintptr_t)to_env->env_ipc_dstva < UTOP) {
		//-E_INVAL if srcva < UTOP but srcva is not page-aligned.
		if (((uintptr_t)srcva % PGSIZE) != 0) {
			return -E_INVAL;
		}
		//-E_INVAL if srcva < UTOP and perm is inappropriate
		ret = check_permissions(perm);
		if (ret) return -E_INVAL;

		//	-E_INVAL if srcva < UTOP but srcva is not mapped in the caller's
		pte_t * pte;
		struct PageInfo * page;
		if (!(page = page_lookup(curenv->env_pgdir, srcva, &pte))) {
			return -E_INVAL;
		}
		// -E_INVAL if (perm & PTE_W), but srcva is read-only in the current environment's address space.
		if ((perm & PTE_W) && !(*pte & PTE_W)) {
			return -E_INVAL;
		}

		if (page_insert(to_env->env_pgdir, page, to_env->env_ipc_dstva, perm) < 0) {
			//-E_NO_MEM if there's not enough memory to map srcva in envid's address space.
			return -E_NO_MEM;
		}

		trans_page = true;
	} 

	// Otherwise, the send succeeds, and the target's ipc fields are
	// updated as follows:

	//    env_ipc_recving is set to 0 to block future sends;
	to_env->env_ipc_recving = 0;
	//    env_ipc_from is set to the sending envid;
	to_env->env_ipc_from = curenv->env_id;
	//    env_ipc_value is set to the 'value' parameter;
	to_env->env_ipc_value = value;
	//    env_ipc_perm is set to 'perm' if a page was transferred, 0 otherwise.
	if (trans_page) to_env->env_ipc_perm = perm;
	else to_env->env_ipc_perm = 0;
	
	// The target environment is marked runnable again
	to_env->env_status = ENV_RUNNABLE;

	return 0;
}

// Block until a value is ready.  Record that you want to receive
// using the env_ipc_recving and env_ipc_dstva fields of struct Env,
// mark yourself not runnable, and then give up the CPU.
//
// If 'dstva' is < UTOP, then you are willing to receive a page of data.
// 'dstva' is the virtual address at which the sent page should be mapped.
//
// This function only returns on error, but the system call will eventually
// return 0 on success.
// Return < 0 on error.  Errors are:
//	-E_INVAL if dstva < UTOP but dstva is not page-aligned.
static int
sys_ipc_recv(void *dstva)
{
	// LAB 4: Your code here.

	// Si dstva es < UTOP => Esperamos recibir una página,
	// En ese caso, validamos que la dirección esté alineada
	if ((uintptr_t)dstva < UTOP) {
		if (PGOFF(dstva)) return -E_INVAL;
	}

	// Indicamos donde queremos recibir la pagina de dato
	curenv->env_ipc_dstva = dstva;

	// Seteamos el proceso como ENV_NOT_RUNNABLE
	curenv->env_status = ENV_NOT_RUNNABLE;

	// Ponemos a true el flag env_ipc_recving
	curenv->env_ipc_recving = true;

	// Como sys_yield() desaloja este proceso y no se llegará a la línea de return 0
	// Escribimos 0 (éxito) en eax, registro de valor de retorno de esta syscall
	curenv->env_tf.tf_regs.reg_eax = 0;

	// Salimos de la CPU, no se volverá a entrar hasta que se reciba el mensaje
	sys_yield();
	// No retorna
	return 0;
}

// Return the current time.
static int
sys_time_msec(void)
{
	// LAB 6: Your code here.
	return time_msec();
	//panic("sys_time_msec not implemented");
}

static int
sys_transmit_packet(char * buffer, size_t size) 
{
	if (!buffer || ((uintptr_t) buffer >= UTOP)) return -E_INVAL;
	return e1000_send_packet(buffer, size);
}

static int
sys_receive_packet(char * buffer, size_t size) 
{
	if (!buffer || ((uintptr_t) buffer >= UTOP)) return -E_INVAL;
	return  e1000_receive_packet(buffer, size);
}

// Dispatches to the correct kernel function, passing the arguments.
int32_t
syscall(uint32_t syscallno, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	// Call the function corresponding to the 'syscallno' parameter.
	// Return any appropriate return value.
	// LAB 3: Your code here.
	switch (syscallno) {
	case SYS_cputs: {
		sys_cputs((const char *) a1, (size_t) a2);
		return 0;
	}
	case SYS_cgetc: {
		return (int32_t) sys_cgetc();
	}
	case SYS_getenvid: {
		return (int32_t) sys_getenvid();
	}
	case SYS_env_destroy: {
		return (int32_t) sys_env_destroy((envid_t) a1);
	}
	case SYS_yield: {
		sys_yield();
		return 0;
	}
	case SYS_exofork: {
		return (int32_t) sys_exofork();
	}
	case SYS_env_set_status: {
		return (int32_t) sys_env_set_status((envid_t)a1, (int)a2);
	}
	case SYS_page_alloc: {
		return (int32_t) sys_page_alloc((envid_t)a1, (void *)a2, (int)a3);
	}
	case SYS_page_map: {
		return (int32_t) sys_page_map((envid_t)a1, (void *)a2, (envid_t)a3, (void *)a4, (int)a5);
	}
	case SYS_page_unmap: {
		return (int32_t) sys_page_unmap((envid_t)a1, (void *)a2);
	}
	case SYS_ipc_recv: {
		return (int32_t) sys_ipc_recv((void *)a1);
	}
	case SYS_ipc_try_send: {
		return (int32_t) sys_ipc_try_send((envid_t)a1, (uint32_t)a2, (void *)a3, (unsigned)a4);
	}
	case SYS_env_set_pgfault_upcall: {
		return (int32_t) sys_env_set_pgfault_upcall((envid_t)a1, (void *)a2);
	}
	case SYS_env_set_trapframe: {
		return (int32_t) sys_env_set_trapframe((envid_t)a1, (struct Trapframe*)a2);
	}
	case SYS_time_msec: {
		return (int32_t) sys_time_msec();
	}
	case SYS_transmit_packet: {
		return (int32_t) sys_transmit_packet((void *)a1, (size_t)a2);
	}
	case SYS_receive_packet: {
		return (int32_t) sys_receive_packet((void *)a1, (size_t) a2);
	}
	default:
		return -E_INVAL;
	}
}
