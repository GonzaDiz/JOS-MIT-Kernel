TP2: Procesos de usuario
========================

env_alloc
---------
Inicializa un nuevo Environment (proceso) que se encuentre libre. Entre otras cosas, le asigna un identificador único. El algoritmo para generar un nuevo identificador es el siguiente:

1. ¿Qué identificadores se asignan a los primeros 5 procesos creados? (Usar base hexadecimal.)

```
	// Generate an env_id for this environment.
	generation = (e->env_id + (1 << ENVGENSHIFT)) & ~(NENV - 1);
	if (generation <= 0)  // Don't create a negative env_id.
		generation = 1 << ENVGENSHIFT;
	e->env_id = generation | (e - envs);
```

Como todos los structs Env se inicializaron en 0 (con meminit), inicialmente tendrán env_id = 0. En la última línea puede observarse una operación `or` en donde el término derecho es una resta entre dos punteros (aritmética de punteros), donde `e` es la dirección de memoria del enviroment libre siendo incializado y `envs` es la base del arreglo de enviroments. Por lo tanto, esta resta no es mas que el offset de la dirección del enviroment libre siendo inicializado.

Los primeros cinco procesos creados tendrán los siguientes identificadores:

```
Identificador número 1: 0x1000 = 4096
Identificador número 2: 0x1001 = 4097
Identificador número 3: 0x1002 = 4098
Identificador número 4: 0x1003 = 4099
Identificador número 5: 0x1004 = 4100
```

2. Supongamos que al arrancar el kernel se lanzan NENV proceso a ejecución. A continuación se destruye el proceso asociado a envs[630] y se lanza un proceso que cada segundo muere y se vuelve a lanzar. ¿Qué identificadores tendrá este proceso en sus sus primeras cinco ejecuciones?

En la primer ejecución, en el momento que se lanzan NENV procesos, el proceso asociado a envs[630] tendrá el identificador 0x1276. Al morir, dicho identificador seguirá asociado al struct de ese proceso. En su próxima ejecución, en el algoritmo de asignación de id, `e->env_id` tendrá el valor antiguo, por lo que la primera línea donde se hace el cálculo para `generation`, dará un valor distinto que para la primera ejecución. En particular, tendrá un aumento de 4096 unidades (decimal) en cada ejecución. Puesto que el `environment index` es siempre el mismo lo que se va modificando es el `Uniqueifier` que distingue procesos con el mismo índice que fueron creados en distintos tiempos.

Por lo que las primeras 5 ejecuciones de ese proceso tienen los siguientes ids:

```
1er env_id: 0x1276 = 4726
2do env_id: 0x2276 = 8822
3er env_id: 0x3276 = 12918
4to env_id: 0x4276 = 17014
5to env_id: 0x5276 = 21110
```

env_init_percpu
---------------

La instrucción `lgdt` ("Load Global Descriptor Table Register") recibe como operando la dirección de un struct del tipo `Pseudodesc`, que no es más que un uint16_t para LÍMITE y un uint32_t para BASE (en total 6 bytes). Donde BASE es la dirección virtual de la gdt (Global Descriptor Table) y LÍMITE es sizeof(gdt) - 1.

Dicha instrucción guarda estos valores (BASE y LÍMITE) en un registro especial de CPU denominado GDTR. Dicho registro, en x86, tiene 48 bits de longitud. Los 16 bits mas bajos indican el tamaño de la GDT y los 32 bits mas altos indican su ubicación en memoria.

```
GDTR:
|LIMIT|----BASE----|
```

Referencia: https://c9x.me/x86/html/file_module_x86_id_156.html


env_pop_tf
----------

Esta función restaura el TrapFrame de un Environment. Un TrapFrame no es mas una estructura que guarda una "foto" del estado de los registros en el momento que se realizó un context switch. Cuando el kernel decide que ese Environment debe volver a ejecución realiza una serie de pasos, y el último de ellos es la función `env_pop_tf()`. El switch siempre se hace desde kernel a user space (nunca de user a user space).

1. ¿Qué hay en `(%esp)` tras el primer `movl` de la función?

El primer `movl` de la función es:
```
movl %0,%%esp
```
Que no hace otra cosa más que hacer que apuntar %esp a el TrapFrame del environment (nuevo tope de stack).
Luego, con `popal` se hace una serie de pops (quitando cosas del nuevo stack, es decir, del TrapFrame) que se van asignando a los registros del CPU.

2. ¿Qué hay en `(%esp)` justo antes de la instrucción `iret`? ¿Y en `8(%esp)`?

Justo antes de la instrucción `iret`, `(%esp)` tiene `uintptr_t tf_eip`. Mientras que en `8(%esp)` tenemos `uint32_t tf_eflags`.

3. ¿Cómo puede determinar la CPU si hay un cambio de ring (nivel de privilegio)?

En la función `env_alloc` (que inicializa un proceso de usuario), se ejecutan las siguientes líneas:

```
	e->env_tf.tf_ds = GD_UD | 3;
	e->env_tf.tf_es = GD_UD | 3;
	e->env_tf.tf_ss = GD_UD | 3;
	e->env_tf.tf_esp = USTACKTOP;
	e->env_tf.tf_cs = GD_UT | 3;
```
Que setean los 2 bits mas bajos del registro de cada segmento, que equivale al 3er ring. Además, se marcan con GD_UD (global descriptor user data) y GD_UT (global descriptor user text).
De esta manera el CPU sabe si el code segment a ejecutar pertenece al usuario o al kernel. Si pertenece al usuario, entonces `iret` restaura los registros SS (stack segment) y ESP (stack pointer). El stack pointer caerá dentro de [USTACKTOP-PGSIZE, USTACKTOP].

gdb_hello
---------
1. Poner un breakpoint en env_pop_tf() y continuar la ejecución hasta allí.
```
(gdb) b env_pop_tf
Punto de interrupción 1 at 0xf0102ead: file kern/env.c, line 514.
(gdb) c
Continuando.
Se asume que la arquitectura objetivo es i386
=> 0xf0102ead <env_pop_tf>:	push   %ebp

Breakpoint 1, env_pop_tf (tf=0xf01c0000) at kern/env.c:514
514	{
```

2. En QEMU, entrar en modo monitor (Ctrl-a c), y mostrar las cinco primeras líneas del comando info registers.
```
(qemu) info registers 
EAX=003bc000 EBX=f01c0000 ECX=f03bc000 EDX=0000023d
ESI=00010094 EDI=00000000 EBP=f0118fd8 ESP=f0118fbc
EIP=f0102ead EFL=00000092 [--S-A--] CPL=0 II=0 A20=1 SMM=0 HLT=0
ES =0010 00000000 ffffffff 00cf9300 DPL=0 DS   [-WA]
CS =0008 00000000 ffffffff 00cf9a00 DPL=0 CS32 [-R-]
```

3. De vuelta a GDB, imprimir el valor del argumento tf

```
(gdb) p tf
$1 = (struct Trapframe *) 0xf01c0000
```

4. Imprimir, con `x/Nx tf` tantos enteros como haya en el struct Trapframe donde N = sizeof(Trapframe) / sizeof(int).
```
(gdb) print sizeof(struct Trapframe) / sizeof(int)
$2 = 17
(gdb) x/17x tf
0xf01c0000:	0x00000000	0x00000000	0x00000000	0x00000000
0xf01c0010:	0x00000000	0x00000000	0x00000000	0x00000000
0xf01c0020:	0x00000023	0x00000023	0x00000000	0x00000000
0xf01c0030:	0x00800020	0x0000001b	0x00000000	0xeebfe000
0xf01c0040:	0x00000023
```

5. Avanzar hasta justo después del `movl ...,%esp`, usando `si M` para ejecutar tantas instrucciones como sea necesario en un solo paso.
```
(gdb) disas
Dump of assembler code for function env_pop_tf:
=> 0xf0102ead <+0>:	push   %ebp
   0xf0102eae <+1>:	mov    %esp,%ebp
   0xf0102eb0 <+3>:	sub    $0xc,%esp
   0xf0102eb3 <+6>:	mov    0x8(%ebp),%esp
   0xf0102eb6 <+9>:	popa   
   0xf0102eb7 <+10>:	pop    %es
   0xf0102eb8 <+11>:	pop    %ds
   0xf0102eb9 <+12>:	add    $0x8,%esp
   0xf0102ebc <+15>:	iret   
   0xf0102ebd <+16>:	push   $0xf010573c
   0xf0102ec2 <+21>:	push   $0x20c
   0xf0102ec7 <+26>:	push   $0xf0105706
   0xf0102ecc <+31>:	call   0xf01000a9 <_panic>
End of assembler dump.
(gdb) si 4
=> 0xf0102eb6 <env_pop_tf+9>:	popa   
0xf0102eb6	515		asm volatile("\tmovl %0,%%esp\n"
(gdb) disas
Dump of assembler code for function env_pop_tf:
   0xf0102ead <+0>:	push   %ebp
   0xf0102eae <+1>:	mov    %esp,%ebp
   0xf0102eb0 <+3>:	sub    $0xc,%esp
   0xf0102eb3 <+6>:	mov    0x8(%ebp),%esp
=> 0xf0102eb6 <+9>:	popa   
   0xf0102eb7 <+10>:	pop    %es
   0xf0102eb8 <+11>:	pop    %ds
   0xf0102eb9 <+12>:	add    $0x8,%esp
   0xf0102ebc <+15>:	iret   
   0xf0102ebd <+16>:	push   $0xf010573c
   0xf0102ec2 <+21>:	push   $0x20c
   0xf0102ec7 <+26>:	push   $0xf0105706
   0xf0102ecc <+31>:	call   0xf01000a9 <_panic>
End of assembler dump.
```


6. Comprobar, con `x/Nx $sp` que los contenidos son los mismos que tf (donde N es el tamaño de tf).

```
(gdb) x/17x $sp
0xf01c0000:	0x00000000	0x00000000	0x00000000	0x00000000
0xf01c0010:	0x00000000	0x00000000	0x00000000	0x00000000
0xf01c0020:	0x00000023	0x00000023	0x00000000	0x00000000
0xf01c0030:	0x00800020	0x0000001b	0x00000000	0xeebfe000
0xf01c0040:	0x00000023
```

7. Explicar con el mayor detalle posible cada uno de los valores. Para los valores no nulos, se debe indicar dónde se configuró inicialmente el valor, y qué representa.

Para explicar cada uno de los valores, se debe entender que a este punto el "stack" tiene la estructura de un Trapframe, que se vió que tiene un tamaño de 17 `ints` (68 bytes). La estructura de un Trapframe es la siguiente:

```
struct Trapframe {
	struct PushRegs tf_regs;
	uint16_t tf_es;
	uint16_t tf_padding1;
	uint16_t tf_ds;
	uint16_t tf_padding2;
	uint32_t tf_trapno;
	/* below here defined by x86 hardware */
	uint32_t tf_err;
	uintptr_t tf_eip;
	uint16_t tf_cs;
	uint16_t tf_padding3;
	uint32_t tf_eflags;
	/* below here only when crossing rings, such as from user to kernel */
	uintptr_t tf_esp;
	uint16_t tf_ss;
	uint16_t tf_padding4;
} __attribute__((packed));
```

Donde la estructura PushRegs se conforma como:

```
struct PushRegs {
	/* registers as pushed by pusha */
	uint32_t reg_edi;
	uint32_t reg_esi;
	uint32_t reg_ebp;
	uint32_t reg_oesp;	
	uint32_t reg_ebx;
	uint32_t reg_edx;
	uint32_t reg_ecx;
	uint32_t reg_eax;
} __attribute__((packed));
```
Las primeras dos líneas de valores de $sp:

```
0xf01c0000:	0x00000000	0x00000000	0x00000000	0x00000000
              reg_edi     reg_esi     reg_ebp     reg_oesp

0xf01c0010:	0x00000000	0x00000000	0x00000000	0x00000000
              reg_ebx     reg_edx     reg_ecx     reg_eax 
```

Son 8 `ints` (32 bytes) y se corresponde con la estructura de PushRegs, que son todos nulos (lógico si es la primera vez que entra en contexto este environment).

Luego, en la tercer línea de valores:

```
0xf01c0020:	0x00000023	0x00000023	0x00000000	0x00000000
		          pad - es    pad - ds    "trapno"    "tf_err" 
```
Los primeros 2 `ints` corresponden a `tf_es` + `tf_padding1` y `tf_ds` + `padding2` respectivamente.
Los valores de `es` y `ds` `(0x0023)` se deben a que en `env_alloc()` se inicializaron con el valor `GD_UD | 3` (Global descriptor number = User Data y 3er ring).

En la cuarta línea de valores tenemos:

```
0xf01c0030:	0x00800020	0x0000001b	0x00000000	0xeebfe000
              tf_eip      pad - cs    tf_eflags   tf_esp
```
El valor de tf_eip (instruction pointer) es la dirección a la primera línea del código ejecutable del environment. Si investigamos el elf con `readelf -S obj/user/hello` se observa lo siguiente:

```
There are 11 section headers, starting at offset 0x7690:

Section Headers:
  [Nr] Name              Type            Addr     Off    Size   ES Flg Lk Inf Al
  [ 0]                   NULL            00000000 000000 000000 00      0   0  0
->[ 1] .text             PROGBITS        00800020 006020 000d19 00  AX  0   0 16 <- (*)
  [ 2] .rodata           PROGBITS        00800d3c 006d3c 000280 00   A  0   0  4
  [ 3] .data             PROGBITS        00801000 007000 000004 00  WA  0   0  4
  [ 4] .bss              NOBITS          00801004 007004 000004 00  WA  0   0  4
  [ 5] .stab_info        PROGBITS        00200000 001000 000010 00  WA  0   0  1
  [ 6] .stab             PROGBITS        00200010 001010 002905 0c   A  7   0  4
  [ 7] .stabstr          STRTAB          00202915 003915 0017ee 00   A  0   0  1
  [ 8] .symtab           SYMTAB          00000000 007004 000440 10      9  25  4
  [ 9] .strtab           STRTAB          00000000 007444 0001fd 00      0   0  1
  [10] .shstrtab         STRTAB          00000000 007641 00004e 00      0   0  1
```

La línea señalada con `-> <- (*)` indica que el text segment, donde se ubica el código ejecutable, comienza en la dirección 0x00800020.

El valor de `cs` `(0x0000001b)` es el resultado de haberlo inicializado como `GD_UT | 3` en `env_alloc()`. Dichos valores setean el Global Descriptor Number como User Text y 3er Ring de privilegios.

El valor de `esp/stack pointer (0xeebfe000)` se corresponde la dirección del stack seteado en `env_alloc()`, que es `USTACKTOP`. Esto es, el tope del stack en el Address Space del environment. Esquema:
```
 *    USTACKTOP  --->  +------------------------------+ 0xeebfe000
 *                     |      Normal User Stack       | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebfd000
```

Por último, la quinta línea:
```
0xf01c0040:	0x00000023
              pad - ss
```

El valor de `ss` (stack segment) se corresponde con lo seteado en env_alloc(), que es exactamente lo mismo que se hizo para `ds` (data segment) y `es` (extra segment).


8. Continuar hasta la instrucción iret, sin llegar a ejecutarla. Mostrar en este punto, de nuevo, las cinco primeras líneas de info registers en el monitor de QEMU. Explicar los cambios producidos.
```
(gdb) disas
Dump of assembler code for function env_pop_tf:
   0xf0102ead <+0>:	push   %ebp
   0xf0102eae <+1>:	mov    %esp,%ebp
   0xf0102eb0 <+3>:	sub    $0xc,%esp
   0xf0102eb3 <+6>:	mov    0x8(%ebp),%esp
=> 0xf0102eb6 <+9>:	popa   
   0xf0102eb7 <+10>:	pop    %es
   0xf0102eb8 <+11>:	pop    %ds
   0xf0102eb9 <+12>:	add    $0x8,%esp
   0xf0102ebc <+15>:	iret   
   0xf0102ebd <+16>:	push   $0xf010573c
   0xf0102ec2 <+21>:	push   $0x20c
   0xf0102ec7 <+26>:	push   $0xf0105706
   0xf0102ecc <+31>:	call   0xf01000a9 <_panic>
End of assembler dump.
(gdb) si 4
=> 0xf0102ebc <env_pop_tf+15>:	iret   
0xf0102ebc	515		asm volatile("\tmovl %0,%%esp\n"
(gdb) disas
Dump of assembler code for function env_pop_tf:
   0xf0102ead <+0>:	push   %ebp
   0xf0102eae <+1>:	mov    %esp,%ebp
   0xf0102eb0 <+3>:	sub    $0xc,%esp
   0xf0102eb3 <+6>:	mov    0x8(%ebp),%esp
   0xf0102eb6 <+9>:	popa   
   0xf0102eb7 <+10>:	pop    %es
   0xf0102eb8 <+11>:	pop    %ds
   0xf0102eb9 <+12>:	add    $0x8,%esp
=> 0xf0102ebc <+15>:	iret   
   0xf0102ebd <+16>:	push   $0xf010573c
   0xf0102ec2 <+21>:	push   $0x20c
   0xf0102ec7 <+26>:	push   $0xf0105706
   0xf0102ecc <+31>:	call   0xf01000a9 <_panic>
End of assembler dump.
```

Anterior:
```
(qemu) info registers
EAX=003bc000 EBX=f01c0000 ECX=f03bc000 EDX=0000023d
ESI=00010094 EDI=00000000 EBP=f0118fd8 ESP=f0118fbc
EIP=f0102ead EFL=00000092 [--S-A--] CPL=0 II=0 A20=1 SMM=0 HLT=0
ES =0010 00000000 ffffffff 00cf9300 DPL=0 DS   [-WA]
CS =0008 00000000 ffffffff 00cf9a00 DPL=0 CS32 [-R-]
```

Actual:
```
(qemu) info registers
EAX=00000000 EBX=00000000 ECX=00000000 EDX=00000000
ESI=00000000 EDI=00000000 EBP=00000000 ESP=f01c0030
EIP=f0102ebc EFL=00000096 [--S-AP-] CPL=0 II=0 A20=1 SMM=0 HLT=0
ES =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
CS =0008 00000000 ffffffff 00cf9a00 DPL=0 CS32 [-R-]
```

Se actualizaron los valores de los registros de propósito general (`EDI`, `ESI`, `EBP`, `EBX`, `EDX`, `ECX` y `EAX`) a los valores traidos del Trapframe. Esto fué gracias a la instrucción `popal`

Se actualizó el valor del registro `ES`. Esto fué gracias a la instrucción `popl %%es`

Se actualizó el valor del registro `DS`. Esto fué gracias a la instrucción `popl %%ds`

El cambio producido en `EIP` se debe a que el instruccion pointer avanzó algunas pocas líneas de código, pero no porque se haya traido del Trapframe.

El code segment no se vió afectado, tampoco los flags.

9. Ejecutar la instrucción iret. En ese momento se ha realizado el cambio de contexto y los símbolos del kernel ya no son válidos.

Imprimir el valor del contador de programa con `p $pc` o `p $eip`

```
(gdb) p $pc
$1 = (void (*)()) 0x800020
```

Cargar los símbolos de hello con `symbol-file obj/user/hello`. Volver a imprimir el valor del contador de programa

```
(gdb) p $pc
$1 = (void (*)()) 0x800020 <_start>
```
Mostrar una última vez la salida de info registers en QEMU, y explicar los cambios producidos.

```
(qemu) info registers
EAX=00000000 EBX=00000000 ECX=00000000 EDX=00000000
ESI=00000000 EDI=00000000 EBP=00000000 ESP=eebfe000
EIP=00800020 EFL=00000002 [-------] CPL=3 II=0 A20=1 SMM=0 HLT=0
ES =0023 00000000 ffffffff 00cff300 DPL=3 DS   [-WA]
CS =001b 00000000 ffffffff 00cffa00 DPL=3 CS32 [-R-]
```

Ahora se actualizaron el `EIP` (Instruction pointer), `CS` (code segment), `EFL` (EFLAGS), y el `SS` (Stack Pointer) a los valores indicados por Trapframe.

10. Poner un breakpoint temporal (tbreak, se aplica una sola vez) en la función syscall() y explicar qué ocurre justo tras ejecutar la instrucción `int $0x30`. Usar, de ser necesario, el monitor de QEMU.
```
(gdb) tbreak syscall
Punto de interrupción temporal 2 at 0x8009ed: file lib/syscall.c, line 23.
(gdb) c
Continuando.
=> 0x8009ed <syscall+17>:	mov    0x8(%ebp),%ecx

Temporary breakpoint 2, syscall (num=0, check=-289415544, a1=4005551752, 
    a2=13, a3=0, a4=0, a5=0) at lib/syscall.c:23
23		asm volatile("int %1\n"
```

Al ejecutar la instrucción `int $0x30` se genera una interrupción que es tomada por el `kernel`.

La información de info registers es:

```
EAX=00000000 EBX=00000000 ECX=00000000 EDX=00000663
ESI=00000000 EDI=00000000 EBP=00000000 ESP=00000000
EIP=0000e062 EFL=00000002 [-------] CPL=0 II=0 A20=1 SMM=0 HLT=0
ES =0000 00000000 0000ffff 00009300
CS =f000 000f0000 0000ffff 00009b00
```
Observar que ahora tanto `ES` como `CS` (code segment) tienen sus últimos bits en 0, lo que significa que se está en el Ring 0 de privilegios (modo `kernel`).


kern_idt
--------

1. ¿Cómo decidir si usar TRAPHANDLER o TRAPHANDLER_NOEC? ¿Qué pasaría si se usara solamente la primera?

Para decidir si usar una u la otra, se debe analizar para cada excepción/interrupción, si para esta el CPU automáticamente hace un push al stack del código de error o no. Para el primer caso se debe utilizar la macro `TRAPHANDLER` y para el segundo `TRAPHANDLER_NOEC`. Si se utilizara solamente la primera, en los casos de excepciones/interrupciones donde el CPU no haga `push` del código de error, el stack basado en el trapframe estaría con un formato inválido, desencadenando en errores graves.

2. ¿Qué cambia, en la invocación de handlers, el segundo parámetro (istrap) de la macro `SETGATE`? ¿Por qué se elegiría un comportamiento u otro durante un syscall?

Con el valor `istrap = 0`, el CPU deshabilita las interrupciones cuando se está en modo kernel. Con `istrap = 1`, el CPU no las desactiva. En `JOS`, no se considerará que el CPU tome interripciones cuando se esté en modo kernel. Otros kernels más avanzados podrían ponerlo a 1.

3. Leer `user/softint.c` y ejecutarlo con `make run-softint-nox`. ¿Qué excepción se genera? Si hay diferencias con la que invoca el programa… ¿por qué mecanismo ocurre eso, y por qué razones?


Al ejecutar `make run-softint-nox` se obtiene lo siguiente por salida estándar:

```
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
TRAP frame at 0xf01c0000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdfd0
  oesp 0xefffffdc
  ebx  0x00000000
  edx  0x00000000
  ecx  0x00000000
  eax  0x00000000
  es   0x----0023
  ds   0x----0023
  trap 0x0000000d General Protection
  err  0x00000072
  eip  0x00800036
  cs   0x----001b
  flag 0x00000082
  esp  0xeebfdfd0
  ss   0x----0023
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
```

Puede oservarse que el valor de `trap` es 0x0000000d que se corresponde con el decimal 13. El `trap` con dicho número es "General Protection" que es causado por "Any memory reference and other protection checks". Esto es diferente a la que se invocó en el programa (14 = page fault). Esto se debe a que en el llamado de la interrupción 14 en `softint.c` se tiene privilegios de modo usuario, mientras que dicha interrupción en el archivo `trap.c`, `SETGATE(idt[T_PGFLT], 0, GD_KT, trap_14, 0);` fue declarada con un nivel de privilegio 0 (el quinto argumento) lo que quiere decir que solo el kernel puede transferir la ejecución a esa interrupción. Si se intenta violar esta regla ocurre una excepción `General Protection` (13) que es justamente la que ocurre.


user_evilhello
--------------

Tenemos la primer versión del programa `evihello.c`:
```
// evil hello world -- kernel pointer passed to kernel
// kernel should destroy user environment in response

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	// try to print the kernel entry point as a string!  mua ha ha!
	sys_cputs((char*)0xf010000c, 100);
}
```
Con el cual tenemos la siguiente salida:
```
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
f�rIncoming TRAP frame at 0xefffffbc
[00001000] exiting gracefully
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
```

Mientras que con la siguiente versión:
```
#include <inc/lib.h>

void
umain(int argc, char **argv)
{
    char *entry = (char *) 0xf010000c;
    char first = *entry;
    sys_cputs(&first, 1);
}
```

Tenemos la siguiente salida:
```
[00000000] new env 00001000
Incoming TRAP frame at 0xefffffbc
[00001000] user fault va f010000c ip 00800039
TRAP frame at 0xf01c0000
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfdfd0
  oesp 0xefffffdc
  ebx  0x00000000
  edx  0x00000000
  ecx  0x00000000
  eax  0x00000000
  es   0x----0023
  ds   0x----0023
  trap 0x0000000e Page Fault
  cr2  0xf010000c
  err  0x00000005 [user, read, protection]
  eip  0x00800039
  cs   0x----001b
  flag 0x00000082
  esp  0xeebfdfb0
  ss   0x----0023
[00001000] free env 00001000
Destroyed the only environment - nothing more to do!
```
1. ¿En qué se diferencia el código de la versión en `evilhello.c` mostrada arriba?
En la segunda versión primero se intenta acceder explícitamente a la dirección `0xf010000c` desde la aplicación de usuario. Dado que esta memoria pertenece al kernel, ocurre aquí el page fault.

2. ¿En qué cambia el comportamiento durante la ejecución? ¿Por qué? ¿Cuál es el mecanismo?
Como vemos para la segunda versión el proceso es destruído a causa del Page Fault. Pero para el primer caso simplemente se le pasa la dirección al kernel y dicha dirección será accedida en modo kernel dentro del handler de la syscall, por ello es que no ocurre ningún Page Fault y el programa termina correctamente. Por esto es necesario que el kernel valide los punteros que envía el usuario como argumentos de las syscalls.



