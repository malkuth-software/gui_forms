using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;
using GuiForms.CompatCapture;

namespace GuiForms.SdrSharpReflectionSystem;

internal static class Program
{
    internal static readonly string[] DefaultRuntimeSupport =
    [
        "System.Formats.Nrbf",
        "Microsoft.Win32.SystemEvents",
        "System.Private.Windows.Core",
        "System.Resources.Extensions",
    ];

    internal static Assembly? DrawingFacade { get; private set; }

    public static int Main(string[] args)
    {
        if (args.Length != 9)
        {
            Console.Error.WriteLine(
                "usage: reflection-runner <bundle> <extract-dir> <forms> <primitives> " +
                "<drawing> <core-runtime-dir> <desktop-runtime-dir> <native-dir> <application-dir>");
            return 2;
        }

        try
        {
            return Run(args);
        }
        catch (Exception error)
        {
            ReportException(error);
            return 1;
        }
    }

    private static int Run(string[] args)
    {
        var bundlePath = Path.GetFullPath(args[0]);
        var extractDirectory = Path.GetFullPath(args[1]);
        var image = File.ReadAllBytes(bundlePath);
        var hash = BundleReader.Sha256(image);
        var expectedHash = Environment.GetEnvironmentVariable(
            "GUI_FORMS_REFLECTION_EXPECTED_SHA256");
        if (!string.IsNullOrWhiteSpace(expectedHash) &&
            !hash.Equals(expectedHash, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Application bundle hash mismatch: {hash}.");

        var bundle = BundleReader.TryRead(image) ??
            throw new BadImageFormatException("The application is not a supported .NET bundle.");
        Directory.CreateDirectory(extractDirectory);
        foreach (var entry in bundle.Entries)
        {
            var path = Path.GetFullPath(Path.Combine(extractDirectory,
                entry.RelativePath.Replace('/', Path.DirectorySeparatorChar)));
            if (!path.StartsWith(extractDirectory + Path.DirectorySeparatorChar,
                                 StringComparison.OrdinalIgnoreCase))
                throw new InvalidDataException("Bundle entry escaped the extraction root.");
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            File.WriteAllBytes(path, BundleReader.ReadEntry(image, entry));
        }
        Console.WriteLine($"bundle=version:{bundle.MajorVersion}.{bundle.MinorVersion}" +
                          $"|entries:{bundle.Entries.Count}|sha256:{hash}");

        PreloadDrawingFacade(Path.GetFullPath(args[4]), Path.GetFullPath(args[7]));
        PreloadDefaultRuntimeSupport(Path.GetFullPath(args[6]));

        var context = new FacadeLoadContext(
            extractDirectory, Path.GetFullPath(args[2]), Path.GetFullPath(args[3]),
            Path.GetFullPath(args[4]), Path.GetFullPath(args[5]),
            Path.GetFullPath(args[6]), Path.GetFullPath(args[7]),
            Path.GetFullPath(args[8]));
        var entryPath = Path.Combine(extractDirectory,
            Path.GetFileNameWithoutExtension(bundlePath) + ".dll");
        if (!File.Exists(entryPath))
            throw new FileNotFoundException("The bundle entry assembly was not extracted.", entryPath);

        var assembly = context.LoadFromAssemblyPath(entryPath);
        Console.WriteLine($"entry={assembly.GetName().Name}|forms={context.FormsIdentity}");
        var workingDirectory = Environment.GetEnvironmentVariable(
            "GUI_FORMS_RUN_WORKING_DIRECTORY");
        var previousWorkingDirectory = Environment.CurrentDirectory;
        if (!string.IsNullOrWhiteSpace(workingDirectory))
        {
            workingDirectory = Path.GetFullPath(workingDirectory);
            Directory.CreateDirectory(workingDirectory);
            Environment.CurrentDirectory = workingDirectory;
        }

        try
        {
            var entryPoint = assembly.EntryPoint ??
                throw new MissingMethodException("The application assembly has no entry point.");
            object?[]? parameters = entryPoint.GetParameters().Length == 0
                ? null : [Array.Empty<string>()];
            var result = entryPoint.Invoke(null, parameters);
            if (result is Task task) task.GetAwaiter().GetResult();
        }
        finally
        {
            Environment.CurrentDirectory = previousWorkingDirectory;
        }
        return 0;
    }

    private static void PreloadDefaultRuntimeSupport(string desktopRuntimeDirectory)
    {
        foreach (var name in DefaultRuntimeSupport)
        {
            var path = Path.Combine(desktopRuntimeDirectory, name + ".dll");
            if (!File.Exists(path)) continue;
            AssemblyLoadContext.Default.LoadFromAssemblyPath(path);
        }
    }

    private static void PreloadDrawingFacade(string drawingPath, string nativeDirectory)
    {
        var assembly = AssemblyLoadContext.Default.LoadFromAssemblyPath(drawingPath);
        NativeLibrary.SetDllImportResolver(assembly, (libraryName, _, _) =>
        {
            if (!libraryName.Equals("gui_drawing_abi0", StringComparison.OrdinalIgnoreCase) &&
                !libraryName.Equals("gui_drawing_raster0", StringComparison.OrdinalIgnoreCase))
                return 0;
            var fileName = Path.HasExtension(libraryName) ? libraryName : libraryName + ".dll";
            var path = Path.Combine(nativeDirectory, fileName);
            return File.Exists(path) ? NativeLibrary.Load(path) : 0;
        });
        DrawingFacade = assembly;
    }

    private static void ReportException(Exception error)
    {
        for (var current = error; current is not null; current = current.InnerException!)
            Console.Error.WriteLine($"failure={current.GetType().FullName}|" +
                current.Message.Replace('\r', ' ').Replace('\n', ' '));
    }
}

internal sealed class FacadeLoadContext : AssemblyLoadContext
{
    private readonly string extractDirectory;
    private readonly string formsPath;
    private readonly string primitivesPath;
    private readonly string drawingPath;
    private readonly string coreRuntimeDirectory;
    private readonly string desktopRuntimeDirectory;
    private readonly string nativeDirectory;
    private readonly string applicationDirectory;

    public FacadeLoadContext(string extractDirectory, string formsPath,
                             string primitivesPath, string drawingPath,
                             string coreRuntimeDirectory,
                             string desktopRuntimeDirectory, string nativeDirectory,
                             string applicationDirectory)
        : base("GUI.Forms application reflection", isCollectible: false)
    {
        this.extractDirectory = extractDirectory;
        this.formsPath = formsPath;
        this.primitivesPath = primitivesPath;
        this.drawingPath = drawingPath;
        this.coreRuntimeDirectory = coreRuntimeDirectory;
        this.desktopRuntimeDirectory = desktopRuntimeDirectory;
        this.nativeDirectory = nativeDirectory;
        this.applicationDirectory = applicationDirectory;
    }

    public string FormsIdentity { get; private set; } = "not-loaded";

    protected override Assembly? Load(AssemblyName assemblyName)
    {
        var name = assemblyName.Name ?? string.Empty;
        if (name.Equals("System.Drawing.Common", StringComparison.OrdinalIgnoreCase))
            return Program.DrawingFacade ??
                throw new InvalidOperationException("Drawing facade was not preloaded.");
        if (Program.DefaultRuntimeSupport.Contains(name, StringComparer.OrdinalIgnoreCase))
            return null;

        var facadePath = name switch
        {
            "System.Windows.Forms" => formsPath,
            "System.Windows.Forms.Primitives" => primitivesPath,
            "System.Drawing.Common" => drawingPath,
            _ => null,
        };
        // The facade's own assemblies (GuiForms.Interop among them) sit
        // beside its System.Windows.Forms.dll.
        var path = facadePath ?? Candidate(Path.GetDirectoryName(formsPath) ?? extractDirectory, name) ??
            Candidate(extractDirectory, name);
        if (path is null && Candidate(coreRuntimeDirectory, name) is not null)
            return null;
        path ??= Candidate(desktopRuntimeDirectory, name);
        if (path is null) return null;

        var loaded = LoadFromAssemblyPath(path);
        if (name == "System.Windows.Forms")
        {
            var token = loaded.GetName().GetPublicKeyToken();
            FormsIdentity = token is null || token.Length == 0 ? "facade-token-null" :
                "unexpected-token-" + Convert.ToHexString(token).ToLowerInvariant();
        }
        return loaded;
    }

    protected override nint LoadUnmanagedDll(string unmanagedDllName)
    {
        foreach (var directory in new[] { nativeDirectory, applicationDirectory })
        {
            var path = Path.Combine(directory, unmanagedDllName);
            if (!Path.HasExtension(path)) path += ".dll";
            if (File.Exists(path)) return LoadUnmanagedDllFromPath(path);
        }
        return 0;
    }

    private static string? Candidate(string directory, string name)
    {
        var path = Path.Combine(directory, name + ".dll");
        return File.Exists(path) ? path : null;
    }
}
