# Mock Kernel Memory

Servidor minimo para probar el modulo CPU sin depender de Kernel Memory real.

## Compilar

```bash
cd cpu/mocks
make
```

Genera:

```bash
cpu/bin/mock_kernel_memory
```

## Ejecutar

```bash
./cpu/bin/mock_kernel_memory 5002
```

El mock:

- acepta una conexion de CPU;
- espera `MSG_CPU_A_KERNEL_MEMORY`;
- responde `MSG_OK`;
- responde `MSG_FETCH_INSTRUCCION` con instrucciones hardcodeadas.

Instrucciones actuales:

```text
PC 0 -> SET AX 2
PC 1 -> SET BX 3
PC 2 -> SUM AX BX
PC 3 -> EXIT
```
