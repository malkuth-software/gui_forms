// The library's file name per platform: the generated imports name the
// logical library (Exports.LibraryName), and this resolver maps it to the file
// cmake/CAbi.cmake builds: gui_forms_abi1.dll on Windows (the major is in the
// name), libgui_forms_abi.so.1 on Linux and libgui_forms_abi.1.dylib on macOS
// (the major is the SOVERSION). It is the one native library a C# program
// loads: self-contained (one module, owner decision 2026-11-15), with the
// engine, the drawing ABI (gd_*) and its raster (the drawing table's
// raster_initialize) inside, importing only system libraries and exporting
// only its gf_* entry points and gd_get_api_v0.
using System;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace GuiForms.Interop;

public static class NativeLibraryName
{
    /// <summary>The file this platform loads for the ABI's major version.</summary>
    public static string file_name()
    {
        if (OperatingSystem.IsWindows())
        {
            return "gui_forms_abi" + Constants.GF_ABI_MAJOR + ".dll";
        }
        if (OperatingSystem.IsMacOS())
        {
            return "libgui_forms_abi." + Constants.GF_ABI_MAJOR + ".dylib";
        }
        return "libgui_forms_abi.so." + Constants.GF_ABI_MAJOR;
    }

    // Registered once, before any import of this assembly binds.
#pragma warning disable CA2255 // A library's module initializer is the documented place for its own resolver.
    [ModuleInitializer]
    internal static void register()
    {
        NativeLibrary.SetDllImportResolver(typeof(NativeLibraryName).Assembly, resolve);
    }
#pragma warning restore CA2255

    private static IntPtr resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName != Exports.LibraryName)
        {
            return IntPtr.Zero;
        }
        return NativeLibrary.Load(file_name(), assembly, searchPath);
    }
}
