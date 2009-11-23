#include <stdio.h>
#include "basetypes.h"
#include "debug_helpers.h"

const char* const kStackTraceFmtString = "%#llx + %#llx %s %s %d + %d\n";
const char* const kDumpFileNameFmtString = 
  "aio_server_minidump_%04d_%02d_%02d_%02d_%02d_%02d.mdmp";

static unsigned char syminfo_buff[sizeof(SYMBOL_INFO) + 1023*sizeof(TCHAR)];

void print_stack_trace(void) {
  STACKFRAME64  stk64;
  IMAGEHLP_LINE64 line_info;
  DWORD disp_from_line;
  HANDLE current_process  = GetCurrentProcess();
  HANDLE current_thread   = GetCurrentThread();

  SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
  if (!SymInitialize(current_process, NULL, TRUE)) {
    return;
  }

  RtlZeroMemory(&stk64, sizeof(stk64));
  
  /*
   * Neat hack to get the EIP. We're using this because
   * RtlCaptureContext() may not be available on every platform.
   */
  __asm {
    lea   ecx, stk64

    dummy_label:
    mov   eax, dummy_label
    mov   dword ptr [ecx]stk64.AddrPC.Offset, eax
    mov   dword ptr [ecx]stk64.AddrPC.Mode, AddrModeFlat
    mov   dword ptr [ecx]stk64.AddrStack.Offset, esp
    mov   dword ptr [ecx]stk64.AddrStack.Mode, AddrModeFlat
    mov   dword ptr [ecx]stk64.AddrFrame.Offset, ebp
    mov   dword ptr [ecx]stk64.AddrFrame.Mode, AddrModeFlat
  }

  fputs("\n+++++++++++++++ Stack trace begins +++++++++++++++\n\n", stdout);
  for ( ; ; ) {

    SYMBOL_INFO* syminfo = (SYMBOL_INFO*) syminfo_buff;
    DWORD64 symdisp;
    /*
     * Strangely providing a CONTEXT structure to StackWalk64 confuses it
     * and makes it skip the last function called before print_stack_trace().
     * Why ???
     */
    BOOL result = StackWalk64(IMAGE_FILE_MACHINE_I386,
                              current_process,
                              current_thread,
                              &stk64,
                              NULL,
                              NULL,
                              SymFunctionTableAccess64,
                              SymGetModuleBase64,
                              NULL);

    if (!result || !stk64.AddrFrame.Offset ) { 
      break; 
    }
    if (!SymGetModuleBase64(current_process,
                            stk64.AddrPC.Offset)) {
     continue;
    } 

    RtlZeroMemory(syminfo, sizeof(SYMBOL_INFO));
    syminfo->SizeOfStruct = sizeof(SYMBOL_INFO);
    syminfo->MaxNameLen = 1024*sizeof(TCHAR);

    result = SymFromAddr(current_process,
                         stk64.AddrPC.Offset,
                         &symdisp,
                         syminfo);
    if (!result) {
      fprintf_s(stdout, 
                kStackTraceFmtString,
                stk64.AddrStack.Offset,
                (LONGLONG) 0,
                "function name not available",
                "file and line info not available",
                0,
                0);
      continue;
    }

    line_info.SizeOfStruct = sizeof(line_info);

    result = SymGetLineFromAddr64(current_process,
                                  syminfo->Address,
                                  &disp_from_line,
                                  &line_info);
    fprintf(stdout, 
            kStackTraceFmtString,
            stk64.AddrStack.Offset,
            (LONGLONG) symdisp,
            syminfo->Name,
            result ? line_info.FileName : "file and line info not available",
            result ? line_info.LineNumber : 0,
            result ? disp_from_line : 0);
    /*
     * TODO : add more detail to the trace ( function parameters ).
     */
  }

  fputs("\n+++++++++++++++ Stack trace ends +++++++++++++++", stdout);
  SymCleanup(current_process);
}

/*!
 * @brief Unhandled exception filter function. This function writes a
 * minidump when an exception happens.
 * @return EXCEPTION_CONTINUE_SEARCH so that the OS will terminate our
 * process.
 */
static LONG __stdcall aio_srv_unhandled_exceptions_filter(
    EXCEPTION_POINTERS* eptrs) {
  HANDLE dump_file = INVALID_HANDLE_VALUE;
  char dumpfile_name[MAX_PATH + 1];
  SYSTEMTIME crash_time;
  MINIDUMP_EXCEPTION_INFORMATION mdeinfo;

  GetLocalTime(&crash_time);
  if (!SUCCEEDED(StringCchPrintfEx(dumpfile_name,
                                   sizeof(dumpfile_name),
                                   NULL,
                                   NULL,
                                   STRSAFE_IGNORE_NULLS,
                                   kDumpFileNameFmtString,
                                   crash_time.wYear,
                                   crash_time.wMonth,
                                   crash_time.wDay,
                                   crash_time.wHour,
                                   crash_time.wMinute,
                                   crash_time.wSecond))) {
    goto FINISH;
  }

  dump_file = CreateFile(dumpfile_name,
                         GENERIC_WRITE,
                         0,
                         NULL,
                         CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
  if (INVALID_HANDLE_VALUE == dump_file) {
    goto FINISH;
  }

  RtlZeroMemory(&mdeinfo, sizeof(mdeinfo));
  mdeinfo.ThreadId = GetCurrentThreadId();
  mdeinfo.ExceptionPointers = eptrs;

  MiniDumpWriteDump(GetCurrentProcess(),
                    GetCurrentProcessId(),
                    dump_file,
                    MiniDumpNormal,
                    &mdeinfo,
                    NULL,
                    NULL);

FINISH :
  if (INVALID_HANDLE_VALUE != dump_file) {
    CloseHandle(dump_file);
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

void set_unhandled_exceptions_filter(void) {
  SetUnhandledExceptionFilter(aio_srv_unhandled_exceptions_filter);
}

void output_formatted_string_to_debugger(const char* file, 
                                         int line, 
                                         const char* fstring,
                                         ...) {
  va_list args_ptr;
  char buff_output[1024];

  va_start(args_ptr, fstring);
  if (FAILED(StringCchPrintfEx(buff_output,
                               sizeof(buff_output),
                               NULL,
                               NULL,
                               STRSAFE_IGNORE_NULLS,
                               "\nFile %s, line %d ",
                               file,
                               line))) {
    goto FINISH;
  }

  OutputDebugString(buff_output);
  if (FAILED(StringCchVPrintfEx(buff_output,
                                sizeof(buff_output),
                                NULL,
                                NULL,
                                STRSAFE_IGNORE_NULLS,
                                fstring,
                                args_ptr))) {
    OutputDebugString("error displaying user data.");
    goto FINISH;
  }

  OutputDebugString(buff_output);
  OutputDebugString("\n");

FINISH :
  va_end(args_ptr);
}
