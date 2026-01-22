//--------------------------------------------------------------
// vm.c
//--------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "vm.h"
#include "term_colors.h"

int g_bGCLog = FALSE;
int g_bVMLog = FALSE;

#if defined(VM_STRESS_GC)
    int g_bGCStress = TRUE;
#else
    int g_bGCStress = FALSE;
#endif


#define GC_LOG(...) (void)(g_bGCLog && fprintf(stderr, __VA_ARGS__))
#define VM_LOG(...) (void)(g_bVMLog && fprintf(stderr, __VA_ARGS__))

CodeChunk* g_current_chunk = NULL;

//-----------------------------------------------
// print an object
//-----------------------------------------------
void vm_print_object(Object* obj, int flags, FILE* stream)
{
    if (!obj)
    {
        fprintf(stream, PRINT_NIL);   // nil
        return;
    }

    if (IS_BOOL(obj))
    {
        if (IS_TRUE(obj))
            fprintf(stream, PRINT_TRUE);
        else
            fprintf(stream, PRINT_FALSE);

        return;
    }

    if (IS_OBJ(obj))
    {
        switch (OBJ_TYPE(obj))
        {
        case OBJ_STRING:
        {
            String* strObj = (String*)obj;
            if (VM_PRINT_PLAIN == flags)
                fprintf(stream, "%s", strObj->value);
            else
                fprintf(stream, "\"%s\"", strObj->value);
        }
        break;

        case OBJ_PAIR:
        {
            Pair* pair = (Pair*)obj;
            if (VM_PRINT_PLAIN == flags)
                fprintf(stream, "(");
            else
                fprintf(stream, "'(");

            vm_print_object(pair->car, flags, stream); // display car

            Object* cdr = pair->cdr;
            while (cdr && IS_OBJ(cdr) && OBJ_TYPE(cdr) == OBJ_PAIR)
            {
                pair = (Pair*)cdr;
                fprintf(stream, " ");
                vm_print_object(pair->car, flags, stream); // display car
                cdr = pair->cdr;
            }

            if (cdr && !IS_NIL(cdr))
            {
                fprintf(stream, " . ");
                vm_print_object(pair->cdr, flags, stream); // display cdr
            }
            fprintf(stream, ")");
        }
        break;

        case OBJ_PROCEDURE:
            fprintf(stream, "#<procedure:%s>", ((String*)((Procedure*)obj)->name)->value);
            break;

        case OBJ_NATIVE_FN:
            fprintf(stream, "#<native_fn:%s>", ((String*)((NativeFunction*)obj)->name)->value);
            break;

        case OBJ_PORT:
        {
            char* str = "<null>";
            Port* port = (Port*)obj;
            if (port && port->fptr && port->name)
                str = (char*)port->name->value;

            fprintf(stream, "#<file:'%s'>", str);
        }
			break;

        default:
            fprintf(stream, "#<obj type=%d>", OBJ_TYPE(obj));
            break;
        }
    }
    else
    {
        // print number
        fprintf(stream, "%d", AS_NUM(obj));
    }
}

//--------------------------------------
// Get the current code chunk
//--------------------------------------
CodeChunk* vm_get_current_chunk()
{
	return g_current_chunk;
}

//--------------------------------------
//
//--------------------------------------
CodeChunk *vm_set_current_chunk(CodeChunk* chunk)
{
    VM_LOG("vm: set current chunk = %p", chunk);

    assert(chunk);

    CodeChunk* previous_chunk = g_current_chunk;
    g_current_chunk = chunk;
    return previous_chunk;
}

//--------------------------------------
// case sensistive string compare
//--------------------------------------
static int compare_fn(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b) == 0;
}

//--------------------------------------------------------------
// Initialize the VM
//--------------------------------------------------------------
int vm_init(VM* pvm) {
    CHECK_THAT(pvm != NULL);

    pvm->sp = 0;
    pvm->fp = 0;
    pvm->ip = 0;
    pvm->return_sp = 0;
    pvm->head = NULL;
    pvm->numObjects = 0;
    pvm->objectsCollected = 0;
    pvm->nextGC = VM_INITIAL_GC_THRESHOLD; // initial threshold
    pvm->allocatedBytes = 0;
    pvm->gc_cycles = 0;

	pvm->max_call_depth = 0;
	pvm->max_stack_size = 0;

    // clear stack
    //memset(pvm->stack, 0, sizeof(pvm->stack));
	//memset(pvm->return_stack, 0, sizeof(pvm->return_stack));

	// init globals - since we use pointer keys, use default hash and compare funcs
    pvm->global_table = ht_create();

	// init interned strings table
    pvm->interned_strings = ht_create();
    ht_set_hash_func(pvm->interned_strings, HT_HASH_STRING);
    ht_set_compare_func(pvm->interned_strings, compare_fn);

    // initialize current scope to NULL (ie. global scope)
    pvm->current_scope = NULL;

    //pvm->vm_code_ptr = 0;
    pvm->peakAllocatedBytes = 0;
    pvm->peakObjects = 0;

    pvm->grayCount = 0;
    pvm->grayCapacity = 0;
    pvm->grayStack = NULL;

	// TODO - pair object pool init
#ifdef VM_USE_FREE_LIST
    pvm->freeObjects = 0;
#endif

    return VM_OK;
}

//--------------------------------------------------------------
// Shutdown the VM and free all resources
//--------------------------------------------------------------
int vm_shutdown(VM* pvm)
{
    CHECK_THAT(pvm != NULL);

    vm_freeObjects(pvm);

    free(pvm->grayStack);
    pvm->grayStack = NULL;

	ht_free(pvm->global_table);
	pvm->global_table = NULL;

    ht_free(pvm->interned_strings);
	pvm->interned_strings = NULL;

    return VM_OK;
}

//--------------------------------------------------------------
// Clear the VM stack
//--------------------------------------------------------------
int vm_clear_stack(VM* pvm)
{
    CHECK_THAT(pvm != NULL);

    pvm->sp = 0;

    return VM_OK;
}

//--------------------------------------------------------------
// Get the current instruction pointer in the current code chunk
//--------------------------------------------------------------
VMCode_t* vm_get_chunk_ip()
{
	return &(g_current_chunk->code[g_current_chunk->count-1]);
}

//--------------------------------------------------------------
// Emit a bytecode instruction
//--------------------------------------------------------------
int vm_emit(VM* pvm, VMCode_t opcode)
{
    if (g_current_chunk == NULL) {
		fprintf(stderr, "No current code chunk set for VM emit!\n");
		exit(1);
	}

    CodeChunk* chunk = g_current_chunk;

    // grow chunk if necessary
	if (chunk->capacity < chunk->count + 1) {
		size_t old_capacity = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(old_capacity);
		chunk->code = (VMCode_t*)vm_alloc(pvm, chunk->code, old_capacity * sizeof(VMCode_t), chunk->capacity * sizeof(VMCode_t));
	}

	chunk->code[chunk->count++] = opcode;
    return (int)chunk->count-1;

	// TODO - line number tracking
	/*
	if (chunk->lines.capacity < chunk->count + 1) {
		int old_capacity = chunk->lines.capacity;
		chunk->lines.capacity = GROW_CAPACITY(old_capacity);
		chunk->lines.data = (int*)realloc(chunk->lines.data, sizeof(int) * chunk->lines.capacity);
	}
	chunk->lines.data[chunk->count - 1] = line;
	*/
}

//--------------------------------------------------------------
// Emit a numeric value as bytecode
//--------------------------------------------------------------
void vm_emit_number(VM* pvm, int val)
{
    vm_emit(pvm, OP_NUM);
    vm_emit(pvm, (VMCode_t)val);
}

//--------------------------------------------------------------
// Emit a string value as bytecode
//--------------------------------------------------------------
void vm_emit_string(VM* pvm, const char* str)
{
    vm_emit(pvm, OP_STRING);
    vm_emit(pvm, (VMCode_t)str);
}

//--------------------------------------------------------------
// Emit an object as bytecode
//--------------------------------------------------------------
void vm_emit_push_object(VM* pvm, Object* obj)
{
    vm_emit(pvm, OP_PUSH);
    vm_emit(pvm, (VMCode_t)obj);
}

//--------------------------------------------------------------
// Clear the VM code buffer
//--------------------------------------------------------------
void vm_clear_code(VM* pvm)
{
    g_current_chunk->count = 0;
}

//--------------------------------------------------------------
// Reset the VM
//--------------------------------------------------------------
int vm_reset(VM* pvm)
{
    CHECK_THAT(pvm != NULL);

    vm_clear_stack(pvm);
    vm_clear_code(pvm);
    
    return VM_OK;
}

//---------------------------------------------------------------
// Disassemble a code chunk for debugging
//---------------------------------------------------------------
void vm_disassemble_chunk(VM* pvm, CodeChunk* chunk, const char* name)
{
    printf("== Disassembly of function '%s' ==\n", name);

    VMCode_t* ip = chunk->code;

    for (; ip < &chunk->code[chunk->count];)
    {
        printf("%p ", ip);
        VMCode_t instruction = *ip++;

        printf(TERM_BRIGHT_BLUE);

        switch (instruction)
        {
        case OP_PUSH:
            printf("PUSH %p\n", (VMCode_t*)*ip++);
            break;

        case OP_POP:
            printf("POP\n");
            break;

        case OP_NUM:
            printf("PUSH NUM %zd\n", *ip++);
            break;

        case OP_STRING:
            printf("STRING '%s'\n", (const char*)*ip++);
            break;

        case OP_TRUE:
            printf("PUSH TRUE\n");
            break;

        case OP_FALSE:
            printf("PUSH FALSE\n");
            break;

        case OP_STORE:
            printf("STORE\n");
            break;

        case OP_LOAD:
            printf("LOAD\n");
            break;

        case OP_DUP:
            printf("DUP\n");
            break;

        case OP_RET:
            printf("RET\n");
            break;

        case OP_ICALL:
            printf("ICALL [SP]\n");
            break;

        case OP_CALL:
        {
#if 0
            Object* fn_obj = (Object*)*ip++;

            if (IS_PROCEDURE(fn_obj))
            {
                Procedure* proc = (Procedure*)fn_obj;
                printf("CALL '%s' %p\n", proc->name->value, proc->chunk.code);
            }
            else
            {
                NativeFunction* proc = (NativeFunction*)fn_obj;
                printf("CALL NATIVE FN '%s'\n", proc->name->value);
            }
#else
            printf("CALL [SP]\n");
#endif
        }
            break;

        case OP_NOP:
            printf("NOP\n");
			break;

        case OP_BRA:
            printf("BRA %p\n", (VMCode_t*)(ip + *ip));
            ip++;
            break;

        case OP_BNT:
            printf("BNT %p\n", (VMCode_t*)(ip + *ip));
            ip++;
            break;

        case OP_BNF:
            printf("BNF %p\n", (VMCode_t*)(ip + *ip));
            ip++;
            break;

        case OP_HALT:
            printf("HALT\n");
            break;

        case OP_LIST:
            printf("MAKE_LIST\n");
            break;

        case OP_PROC:
            printf("PROC\n");
            break;

        case OP_CLOSURE:
            printf("CLOSURE\n");
            break;

        case OP_GET_LOCAL:
            printf("GET_LOCAL\n");
            break;
            
        case OP_SET_LOCAL:
            printf("SET_LOCAL\n");
            break;

        case OP_PRINT_TOS:
            printf("PRINT_TOS\n");
			break;

        default:
            printf("Unknown opcode: %zd\n", instruction);
            break;
        }

        printf(TERM_RESET);
    }
}

//---------------------------------------------------------------
// Execute the VM code starting from the given address
//---------------------------------------------------------------
void vm_exec(VM* pvm, VMCode_t *addr)
{
    Object* obj;

    if (pvm == NULL) {
        fprintf(stderr, "VM is NULL!\n");
        return;
    }
    
    if (!addr) {
        fprintf(stderr, "Invalid VM code address: %p\n", addr);
        return;
    }

    VM_LOG("\n---- VM Execution Trace ----\n");

    pvm->ip = addr;

    while(1)
    {
        VM_LOG("IP=%p SP=%d RP=%d: ", pvm->ip, pvm->sp, pvm->return_sp);

        VMCode_t opcode = *pvm->ip++;

        switch(opcode)
        {
        case OP_PUSH:
            VM_LOG("PUSH obj\n");
            vm_push(pvm, (Object*)*pvm->ip++);
            break;

        case OP_POP:
            VM_LOG("POP obj\n");
            vm_pop(pvm);
            break;

        case OP_DUP:
            VM_LOG("DUP\n");
            obj = vm_peek(pvm, 0);
            vm_push(pvm, obj);
			break;

        case OP_NUM:
            VM_LOG("PUSH NUM %zu\n", *pvm->ip);
            vm_push(pvm, MAKE_NUM(*pvm->ip++));
            break;

        case OP_STRING:
            VM_LOG("PUSH STRING '%s'\n", (const char*)*pvm->ip);
            obj = vm_new_string(pvm, (const char*)*pvm->ip++);
            vm_push(pvm, obj);
            break;

        case OP_RET:
            VM_LOG("RET\n");
            {
				Object* nargs_obj = vm_pop(pvm);    // pop number of args
				Object* ret_val = vm_pop(pvm);      // get return value
				Object* nframe_obj = vm_pop(pvm);   // pop old frame pointer
				pvm->fp = AS_NUM(nframe_obj);       // restore old frame pointer
				vm_pop_n(pvm, AS_NUM(nargs_obj));   // pop args from stack
                vm_push(pvm, ret_val);              // restore return value to stack

                // set IP to return address
                pvm->ip = vm_pop_ip(pvm);
            }
            break;

        case OP_HALT:
            VM_LOG("HALT\n");
            return;

        case OP_TRUE:
            VM_LOG("PUSH TRUE\n");
            vm_pushBoolean(pvm, TRUE);
            break;

        case OP_FALSE:
            VM_LOG("PUSH FALSE\n");
            vm_pushBoolean(pvm, FALSE);
            break;

        case OP_ICALL:
        {
            Object* fn_obj = vm_pop(pvm);   // get function object
                
            // indirect call to address on stack
            VM_LOG("ICALL %p\n", (VMCode_t*)(pvm->ip + *pvm->ip));

            VMCode_t* addr = (VMCode_t*)*pvm->ip++;
            vm_push_ip(pvm, pvm->ip);   // push return address onto stack

            // call function by setting IP to function address
            pvm->ip = addr;
        }
            break;

        case OP_CALL:
        {
            Object* fn_obj = vm_pop(pvm);   // get function object

            if (IS_PROCEDURE(fn_obj))
            {
                Procedure* proc = (Procedure*)fn_obj;
                VM_LOG("CALL '%s' %p\n", proc->name->value, proc->chunk.code);

                Object* n = vm_pop(pvm);
                int nargs = AS_NUM(n);

                if (proc->arity != nargs)
                {
                    yyerror("incorrect number of arguments to function");

                    vm_pop_n(pvm, nargs);   // clear args from stack
                    pvm->ip++;              // skip to the start of next instruction
                    break;
                }

                vm_push_ip(pvm, pvm->ip);   // push return address onto stack

                // call function by setting IP to function address
                pvm->ip = proc->chunk.code;

                vm_push(pvm, MAKE_NUM(pvm->fp));    // push old frame pointer onto stack
                pvm->fp = pvm->sp;                  // set-up new frame pointer
            }
            else if (IS_NATIVE_FN(fn_obj))
            {
                NativeFunction* proc = (NativeFunction*)fn_obj;
                VM_LOG("CALL NATIVE FN '%s'\n", proc->name->value);
                SKFunc func = (SKFunc)proc->func;

                if (func)
                {
                    int arity = proc->arity;
                    if (arity >= 0)
                    {
                        Object* n = vm_pop(pvm);
                        int nargs = AS_NUM(n);
                        if (nargs != arity)
                        {
                            yyerror("incorrect number of arguments to native function");

                            // clear args from stack
                            vm_pop_n(pvm, nargs);
                            break;
                        }
                    }

                    func((SKState*)1); // TODO - In a real implementation, pass the VM or context
                }
                else {
                    fprintf(stderr, "NULL function pointer in NATIVE call!\n");
                }
            }
            else
                yyerror("procedure expected!");
        }
            break;

        case OP_BNT:
            VM_LOG("BNT %p\n", (VMCode_t*)(pvm->ip + *pvm->ip));
            obj = vm_pop(pvm);
            if (IS_FALSE(obj)) 
            {
                // branch to new address
                pvm->ip += *pvm->ip;
            }
            else 
            {
                // move to next instruction in sequence
                pvm->ip++;
            }
            break;

        case OP_BNF:
            VM_LOG("BNF %p\n", (VMCode_t*)(pvm->ip + *pvm->ip));
            obj = vm_pop(pvm);
            if (!IS_FALSE(obj))
            {
                // branch to new address
                pvm->ip += *pvm->ip;
            }
            else
            {
                // move to next instruction in sequence
                pvm->ip++;
            }
            break;

        case OP_NOP:
            VM_LOG("NOP\n");
            // do nothing!
            break;

        case OP_BRA:
        {
            VM_LOG("BRA %p\n", (VMCode_t*)(pvm->ip + *pvm->ip));
            pvm->ip += *pvm->ip;
        }
			break;

        case OP_STORE:
        {
            // variable assignment
            Object *value = vm_pop(pvm);
            Object *key = vm_pop(pvm);
            if (!IS_STRING(key))
            {
                yyerror("invalid variable name");
                printf("STORE: key is not a string! Obj type = %d\n", OBJ_TYPE(key));
                break;
            }

            VM_LOG("STORE '%s'\n", ((String*)key)->value);
            vm_update_global(pvm, key, value);
        }
			break;

        case OP_LOAD:
        {
            Object* key = vm_pop(pvm);
            VM_LOG("LOAD '%s'\n", ((String*)key)->value);
            // variable retrieval
            Object* value = vm_get_global(pvm, key);
            
			// if variable not found, error
			// TODO NB: the hash table doesn't differentiate between NULL value and missing key
            //if (!value)
            //    yyerror("entity undefined");

            vm_push(pvm, value);
        }
            break;

        case OP_GET_LOCAL:
            VM_LOG("GET_LOCAL\n");
            {
                Object* index_obj = vm_pop(pvm);
                int index = AS_NUM(index_obj);              // get stack offset of parameter
                Object* local_var = vm_fp_peek(pvm, index); // get parameter
                vm_push(pvm, local_var);                    // push it onto the stack
			}
            break;

        case OP_LIST:
        {
            VM_LOG("MAKE_LIST\n");
            Pair* alist = NULL;
            Pair* pair = NULL;

            obj = vm_pop(pvm);
			int nargs = AS_NUM(obj);
            for (int i = 0; i < nargs; i++)
            {
                vm_push(pvm, (Object*)pair);    // stash pair on stack to prevent premature GC
                alist = (Pair*)vm_newObject(pvm, sizeof(Pair), OBJ_PAIR);
                vm_pop(pvm);                    // discard stashed pair
                alist->car = vm_pop(pvm);
                alist->cdr = (Object*)pair;
                pair = alist;
            }

			// push the constructed list onto the stack
            vm_push(pvm, (Object*)alist);
        }
            break;

        case OP_PRINT_TOS:
            VM_LOG("PRINT_TOS\n");

            if (pvm->sp)
            {
                fprintf(stdout, TERM_BRIGHT_BLUE);
                    vm_print_object(vm_pop(pvm), VM_PRINT_FANCY, stdout);
                fprintf(stdout, TERM_RESET"\n");
            }

            break;

        default:
            printf("Unknown opcode: %zu\n", opcode);
            break;
        }
    }
}

//--------------------------------------------------------------
// Mark an object
//--------------------------------------------------------------
static void markObject(VM* pvm, Object* object) {
    if (!IS_OBJ(object)) return;

    if (IS_MARKED(object)) return;

	MARK_OBJ(object);

    if (pvm->grayCapacity < pvm->grayCount + 1) 
    {
        pvm->grayCapacity = GROW_CAPACITY(pvm->grayCapacity);
        pvm->grayStack = (Object**)realloc(pvm->grayStack, sizeof(Object*) * pvm->grayCapacity);

        // fatal error
        if (pvm->grayStack == NULL) exit(1);
    }

    pvm->grayStack[pvm->grayCount++] = object;
}

//--------------------------------------------------------------
// Mark all reachable objects
//--------------------------------------------------------------
static void markObjects(VM* pvm) {
    GC_LOG("-- gc marking\n");
    // Recursively mark all reachable objects

    size_t index = 0;
    ht_key_t key;
    ht_value_t value;

    // mark global objects
    while(ht_next(pvm->global_table, &index, &key, &value))
    {
        markObject(pvm, (Object*)key);
        markObject(pvm, (Object*)value);
    }

    // mark stack objects
    for (int i = 0; i < pvm->sp; i++) 
    {
        markObject(pvm, pvm->stack[i]);
    }

    // mark environment chain
    Environment* env = pvm->current_scope;
    while (env)
    {
        // mark environment object
        markObject(pvm, (Object*)env);

        // mark all bindings in the environment
        size_t env_index = 0;
        ht_key_t env_key;
        ht_value_t env_value;
        while (ht_next(env->bindings, &env_index, &env_key, &env_value))
        {
            markObject(pvm, (Object*)env_key);
            markObject(pvm, (Object*)env_value);
        }
        env = env->parent;
    }
}

//--------------------------------------------------------------
// Free an object
//--------------------------------------------------------------
static void freeObject(VM* pvm, Object* object)
{
	GC_LOG("-- gc freeing object of type %d\n", OBJ_TYPE(object));

    // we can only free Objects
    if (!IS_OBJ(object))
		return;

	// clean up based on object type
    switch (OBJ_TYPE(object))
    {
    case OBJ_PROCEDURE:
        {
            // free function-specific resources
            Procedure * proc = (Procedure*)object;
            GC_LOG("-- gc freeing proc: '%s'\n", ((String*)proc->name)->value);
            vm_alloc(pvm, proc->chunk.code, proc->chunk.capacity * sizeof(VMCode_t), 0);
        }
		break;
    
    case OBJ_ENVIRONMENT:
        {
            // free environment-specific resources
            Environment* env = (Environment*)object;
            ht_free(env->bindings);
        }
        break;

    case OBJ_PORT:
    {
        Port* port = (Port*)object;
        GC_LOG("-- gc closing file: '%s'\n", ((String*)port->name)->value);

        if (port->fptr)
            fclose(port->fptr);

        port->fptr = NULL;
        port->name = NULL;
    }
        break;

    default:
        // nothing to do!
        break;
    }

    // track objects collected
    pvm->objectsCollected++;

    // Free the memory associated with the object
    vm_alloc(pvm, object, object->size, 0);
}

//--------------------------------------------------------------
// Sweep unmarked objects
//--------------------------------------------------------------
static int sweepObjects(VM* pvm) {
    GC_LOG("-- gc sweeping\n");

    int free_count = 0;

    Object** obj = &pvm->head;
    while (*obj) 
    {
        if (!IS_MARKED(*obj)) 
        {
            Object* unreached = *obj;
            *obj = unreached->next;

            freeObject(pvm, unreached);
            //vm_alloc(pvm, unreached, unreached->size, 0);
            pvm->numObjects--;
            free_count++;
        } else 
        {
            UNMARK_OBJ(*obj);   // Reset mark for next GC cycle
            obj = &(*obj)->next;
        }
    }

    GC_LOG("-- gc (%d) objects freed\n", free_count);

    return free_count;
}

//-------------------------------------------------------------
// Mark an object and add it to the gray stack
//-------------------------------------------------------------
static void blackenObject(VM* pvm, Object* object) 
{
    // Mark the object's references and add them to the gray stack
    // if they are not already marked
    //GC(_LOG("-- gc blackening object\n");

    if (!IS_OBJ(object)) return;

    switch(object->type) 
    {
    case OBJ_PAIR:
        {
            Pair* pair = (Pair*)object;
            markObject(pvm, pair->car);
            markObject(pvm, pair->cdr);
        }
        break;

    case OBJ_STRING:
        // Strings have no references to mark
        break;

    case OBJ_NATIVE_FN:
        {
            NativeFunction* nativeFn = (NativeFunction*)object;

            // If the native function has any references, mark them here

            // mark the function name object
            markObject(pvm, (Object*)nativeFn->name);
        }
        break;

    case OBJ_PROCEDURE:
        {
            Procedure* proc = (Procedure*)object;

            // mark any constants or references in the function's code chunk if needed

            // mark the function's name object
            markObject(pvm, (Object*)proc->name);
        }
        break;

    case OBJ_CLOSURE:
		{
            Closure* closure = (Closure*)object;
            markObject(pvm, closure->function);
			
            // Closure-specific marking logic
			// e.g., mark captured variables
		}
        break;

    case OBJ_ENVIRONMENT:
        {
        }
	    break;

    case OBJ_PORT:
        {
            markObject(pvm, (Object*)((Port*)object)->name);
        }
        break;

    default:
        // Handle other object types as needed
        break;
    }
}

//--------------------------------------------------------------
// Trace references from the gray stack
//--------------------------------------------------------------
static void traceReferences(VM* pvm) {
    // Trace references from the gray stack and mark reachable objects
    GC_LOG("-- gc tracing references\n");

    while (pvm->grayCount > 0) 
    {
        Object* object = pvm->grayStack[--pvm->grayCount];
        blackenObject(pvm, object);
    }
}

//--------------------------------------------------------------
// Remove unmarked objects from the interned string table
//--------------------------------------------------------------
static void removeWhiteObjects(VM* pvm) {
    size_t index = 0;
    ht_key_t key;
    ht_value_t value;
    int UNUSED removed = 0;

    // only remove unmarked strings
    while(ht_next(pvm->interned_strings, &index, &key, &value))
    {
        if (!IS_OBJ((Object*)value) || IS_MARKED((Object*)value))
            continue;

        if (HT_OK == ht_remove(pvm->interned_strings, key))
        {
            removed++;
            GC_LOG("-- gc removing interned string: '%s'\n", ((String*)value)->value);
        }
        else
            fprintf(stderr, "Error removing interned string during GC\n");
    }

    GC_LOG("-- gc removing (%d) white objects from intern table\n", removed);
}

//--------------------------------------------------------------
// Perform garbage collection
// Mark-and-sweep algorithm
//--------------------------------------------------------------
void vm_gc(VM* pvm) {
    GC_LOG("-- gc begin\n");

    size_t before UNUSED = pvm->allocatedBytes;

    markObjects(pvm);

    // if there are any references from the stack or globals, we need to
    // follow those references and mark them as well
    traceReferences(pvm);

    // remove strings from the intern table that are not marked
    removeWhiteObjects(pvm);

    int freed UNUSED = sweepObjects(pvm);

    pvm->gc_cycles++;
    
    GC_LOG("-- gc end [%d / %zu] objs freed/live, [%zu / %zu] bytes freed/alloc'd, next at %zu.\n\n", freed, pvm->numObjects, before - pvm->allocatedBytes, pvm->allocatedBytes, pvm->nextGC);
}

//--------------------------------------------------------------
// Push a string onto the stack
//--------------------------------------------------------------
String *vm_push_string(VM* pvm, const char *str)
{
	Object* string_obj = vm_new_string(pvm, str);
    vm_push(pvm, string_obj);
	return (String*)string_obj;
}

//--------------------------------------------------------------
// Push a global variable onto the stack
//--------------------------------------------------------------
void vm_push_global(VM* pvm, const char *name)
{
    vm_push(pvm, vm_get_global(pvm, vm_new_string(pvm, name)));
}

//--------------------------------------------------------------
// Push a return address onto the return stack
//--------------------------------------------------------------
void vm_push_ip(VM* pvm, VMCode_t *ip)
{
	if (pvm->return_sp >= VM_STACK_SIZE) {
		fprintf(stderr, "Return stack overflow!\n");
		return;
	}

	pvm->return_stack[pvm->return_sp++] = ip;

    if (pvm->return_sp > pvm->max_call_depth) {
        pvm->max_call_depth = pvm->return_sp;
    }
}

//--------------------------------------------------------------
// Pop a return address from the return stack
//--------------------------------------------------------------
VMCode_t* vm_pop_ip(VM* pvm) 
{
	if (pvm->return_sp <= 0) {
		fprintf(stderr, "Return stack underflow!\n");
		return NULL;
	}

	return pvm->return_stack[--pvm->return_sp];
}

//--------------------------------------------------------------
// Push an object onto the stack
//--------------------------------------------------------------
void vm_push(VM* pvm, Object* obj) {
    if (pvm->sp >= VM_STACK_SIZE) {
        fprintf(stderr, "Stack overflow!\n");
        return;
    }

    pvm->stack[pvm->sp++] = obj;

    if (pvm->sp > pvm->max_stack_size) {
        pvm->max_stack_size = pvm->sp;
	}
}

//--------------------------------------------------------------
// Pop an object from the stack
//--------------------------------------------------------------
Object* vm_pop(VM* pvm) {
    if (pvm->sp <= 0) {
        fprintf(stderr, "Stack underflow!\n");
        return NULL;
    }

    return pvm->stack[--pvm->sp];
}

//--------------------------------------------------------------
// Pop n objects from the stack
//--------------------------------------------------------------
void vm_pop_n(VM* pvm, int n)
{
    if (pvm->sp - n < 0) {
        fprintf(stderr, "Stack underflow on pop_n!\n");
        return;
    }
    pvm->sp -= n;
}

//--------------------------------------------------------------
// Peek at an object on the stack without popping it
//--------------------------------------------------------------
Object* vm_peek(VM* pvm, int distance) {
    if (pvm->sp - 1 - distance < 0) {
        fprintf(stderr, "Stack underflow on peek!\n");
        return NULL;
    }

    return pvm->stack[pvm->sp - 1 - distance];
}

//--------------------------------------------------------------
// Peek at an object relative to the frame without popping it
//--------------------------------------------------------------
Object* vm_fp_peek(VM* pvm, int distance) {
    if (pvm->fp - 1 - distance < 0) {
        fprintf(stderr, "Stack underflow on fp_peek!\n");
        return NULL;
    }

    return pvm->stack[pvm->fp - 1 - distance];
}

//--------------------------------------------------------------
// Custom realloc function for memory management
//--------------------------------------------------------------
void *vm_alloc(VM* pvm, void *ptr, size_t old_size, size_t new_size) {
    // nothing to do!
    if (new_size == old_size) {
        fprintf(stderr, "Realloc called with same size!\n");
        return ptr;
    }

    // possibly trigger GC
    if (new_size > old_size)
    {
        // Force a garbage collection on every allocation if stress GC is enabled
        // This is useful for testing purposes
        do
        {
            if (g_bGCStress)
                vm_gc(pvm);
        } while (0);

    }

    // free the memory
    if (new_size == 0) {
        free(ptr);

        // update allocatedBytes
        pvm->allocatedBytes -= old_size;

        return NULL;
    }

    // Allocate more or less memory
    void *new_ptr = realloc(ptr, new_size);

    if (!new_ptr) {
        fprintf(stderr, "Memory allocation failed!\n");
        exit(1);
    }

    // Update allocated and peak bytes
    pvm->allocatedBytes += new_size - old_size;
    if (pvm->allocatedBytes > pvm->peakAllocatedBytes) {
        pvm->peakAllocatedBytes = pvm->allocatedBytes;
    }

    // update gc threshold
    if (pvm->allocatedBytes > pvm->nextGC) {
        vm_gc(pvm);
        pvm->nextGC = pvm->allocatedBytes * VM_GC_HEAP_GROWTH_FACTOR;
    }

    // return the new pointer
    return new_ptr;
}

//--------------------------------------------------------------
// Create a new object and add it to the VM's object list
//--------------------------------------------------------------
Object* vm_newObject(VM* pvm, size_t size, int type)
{
    if (size == 0)
        size = sizeof(Object);

    Object* obj = (Object*)vm_alloc(pvm, NULL, 0, size);
	UNMARK_OBJ(obj);        // new objects are unmarked
    obj->type = type;       // default type
	obj->size = size;       // set size
    obj->next = pvm->head;  // Insert at the head of the object list
	pvm->head = obj;        // Update head to new object
	pvm->numObjects++;      // Increment object count

    // update peak objects count
    if (pvm->numObjects > pvm->peakObjects) {
        pvm->peakObjects = pvm->numObjects;
    }

    return obj;
}

//--------------------------------------------------------------
// Create a new environment object
//--------------------------------------------------------------
Object *vm_new_environment(VM* pvm)
{
    Environment* env = (Environment*)vm_newObject(pvm, sizeof(Environment), OBJ_ENVIRONMENT);
    env->bindings = ht_create();
    env->parent = pvm->current_scope;
    pvm->current_scope = env;

    return (Object*)env;
}

//--------------------------------------------------------------
// Unlink the top environment object from the environment chain
//--------------------------------------------------------------
void vm_free_environment(VM* pvm)
{
    if (pvm->current_scope)
        pvm->current_scope = pvm->current_scope->parent;

    // no need to explicitly free, GC will free this object once it has no refs
    // ht_free(&env->env);
    // vm_alloc(pvm, env, sizeof(Environment), 0);
}

//--------------------------------------------------------------
// Create a new boolean object
//--------------------------------------------------------------
Object* vm_newBoolean(VM* pvm, int val)
{
    if (val)
        return MAKE_TRUE();

    return MAKE_FALSE();
}

//--------------------------------------------------------------
// Push a boolean object onto the stack
//--------------------------------------------------------------
void vm_pushBoolean(VM* pvm, int val)
{
    Object* obj = vm_newBoolean(pvm, val);
    vm_push(pvm, obj);
}

//--------------------------------------------------------------
// Get an interned string object
//--------------------------------------------------------------
Object *vm_get_string(VM* pvm, const char* str)
{
    if (!pvm || !str) return NULL;

    // check interned strings table first
    ht_value_t val = ht_find(pvm->interned_strings, str);

    return (Object*)val;
}

//--------------------------------------------------------------
// Create a new string object
//--------------------------------------------------------------
Object *vm_new_string(VM* pvm, const char* str)
{
    if (!pvm || !str) return NULL;

    // check interned strings table first
	ht_value_t val = vm_get_string(pvm, str);
    
    // if string obj already exists, return it
    if (val)
        return (Object*)val;

    // otherwise create new string object
    Object* obj = vm_newObject(pvm, sizeof(String) + strlen(str) + 1, OBJ_STRING);

    String* stringObj = (String*)obj;
    strcpy((char*)stringObj->value, str);

    // and add to interned strings table
    ht_insert(pvm->interned_strings, stringObj->value, stringObj);

    return obj;
}

//--------------------------------------------------------------
// Initialize a code chunk
//--------------------------------------------------------------
void vm_init_chunk(CodeChunk* chunk)
{
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->code = NULL;
    //chunk->lines = NULL;
}

//--------------------------------------------------------------
// Free a code chunk
//--------------------------------------------------------------
void vm_free_chunk(VM* pvm, CodeChunk* chunk)
{
	vm_alloc(pvm, chunk->code, chunk->capacity * sizeof(VMCode_t), 0);
	chunk->code = NULL;
	chunk->count = 0;
	chunk->capacity = 0;
}

//--------------------------------------------------------------
// Create a new procedure object
//--------------------------------------------------------------
Object* vm_new_proc(VM* pvm)
{
    if (!pvm) return NULL;

    // create new procedure object
    Object* obj = vm_newObject(pvm, sizeof(Procedure), OBJ_PROCEDURE);

    Procedure* proc = AS_OBJ(Procedure, obj);
    if (!proc)
        return NULL;

    proc->arity = 0;

	vm_init_chunk(&proc->chunk);

    return obj;
}

//--------------------------------------------------------------
// Define a global object
//--------------------------------------------------------------
int vm_define_global(VM *pvm, Object* key, Object *value)
{
    // determine if we can redefine a global
#ifdef VM_NO_REDEFINE
    return ht_insert(pvm->global_table, key, value);
#else
    return ht_add(pvm->global_table, key, value);
#endif

}

//--------------------------------------------------------------
// Update a global object
//--------------------------------------------------------------
int vm_update_global(VM *pvm, Object* key, Object *value)
{
    return ht_add(pvm->global_table, key, value);
}

//--------------------------------------------------------------
// Get a global object by key
//--------------------------------------------------------------
Object *vm_get_global(VM *pvm, Object* key)
{
    ht_value_t val = ht_find(pvm->global_table, key);
    return (Object*)val;
}

//--------------------------------------------------------------
// Set a variable in current scope
//--------------------------------------------------------------
void vm_set_var(VM* pvm, const char* name, Object* value)
{
    Object* key = vm_new_string(pvm, name);

    if (pvm->current_scope)
    {
        // set in current environment
        ht_add(pvm->current_scope->bindings, key, value);
        return;
	}
    else
        vm_update_global(pvm, key, value);
}

//--------------------------------------------------------------
// Get a variable from current scope
//--------------------------------------------------------------
Object* vm_get_local_var(VM* pvm, const char* name)
{
    Object* key = vm_new_string(pvm, name);

    if (pvm->current_scope)
    {
        // look in current environment chain
        Environment* env = pvm->current_scope;
        ht_value_t val;
        while (env)
        {
            val = ht_find(env->bindings, key);
            if (val)
                return (Object*)val;
            env = env->parent;
        }
	}

    return NULL;    // vm_get_global(pvm, key);
}

//--------------------------------------------------------------
// Print the current VM stack for debugging
//--------------------------------------------------------------
void vm_print_stack(VM* pvm)
{
    printf("---- VM Stack (SP=%d) ----\n", pvm->sp);
    for (int i = pvm->sp - 1; i >= 0; i--)
    {
        Object* obj = pvm->stack[i];
        if (IS_NUM(obj))
        {
            printf("[%d] NUM: %d\n", i, AS_NUM(obj));
        }
        else if (IS_STRING(obj))
        {
            printf("[%d] STRING: '%s'\n", i, AS_STRING(obj)->value);
        }
        else
        {
            printf("[%d] OBJ TYPE: %d\n", i, OBJ_TYPE(obj));
        }
    }
    printf("-------------------------\n");
}

//--------------------------------------------------------------
// Create a new port object
//--------------------------------------------------------------
Port *vm_new_port(VM* pvm, const char* name, FILE* fptr)
{
    Port* port = (Port*)vm_newObject(pvm, sizeof(Port), OBJ_PORT);
    vm_push(pvm, (Object*)port); // save on stack for GC

    port->name = (String*)vm_new_string(pvm, name);
    port->fptr = fptr;

    vm_pop(pvm);
    return port;
}

//--------------------------------------------------------------
// Create default ports (stdin, stdout, stderr)
//--------------------------------------------------------------
int vm_create_default_ports(VM* pvm)
{
	Port* input_port, * output_port, * error_port;

	input_port = (Port*)vm_new_port(pvm, "stdin", stdin);
    vm_push(pvm, (Object*)input_port);
    vm_define_global(pvm, (Object*)vm_new_string(pvm, "current-input-port"), (Object*)input_port);

    output_port = (Port*)vm_new_port(pvm, "stdout", stdout);
	vm_push(pvm, (Object*)output_port);
    vm_define_global(pvm, (Object*)vm_new_string(pvm, "current-output-port"), (Object*)output_port);

	error_port = (Port*)vm_new_port(pvm, "stderr", stderr);
	vm_push(pvm, (Object*)error_port);
    vm_define_global(pvm, (Object*)vm_new_string(pvm, "current-error-port"), (Object*)error_port);

    vm_pop_n(pvm, 3); // pop the ports off the stack

    return TRUE;
}

//--------------------------------------------------------------
// Free all objects in the VM
//--------------------------------------------------------------
void vm_freeObjects(VM* pvm) {
    Object* obj = pvm->head;

    while (obj) {
        Object* next = obj->next;
        free(obj);  // TODO - use vm_alloc for obj dtors?
        obj = next;
    }

    free(pvm->grayStack);
    pvm->grayStack = NULL;
    pvm->grayCount = 0;
    pvm->grayCapacity = 0;
    pvm->head = NULL;
    pvm->numObjects = 0;
    pvm->allocatedBytes = 0;
    pvm->peakAllocatedBytes = 0;
    pvm->peakObjects = 0;
    pvm->gc_cycles = 0;
    pvm->max_call_depth = 0;
    pvm->max_stack_size = 0;
    pvm->sp = 0;
    pvm->fp = 0;
    pvm->return_sp = 0;
    pvm->objectsCollected = 0;
}

