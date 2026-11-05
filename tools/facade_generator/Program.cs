using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Text.RegularExpressions;

if (args.Length != 3)
{
    Console.Error.WriteLine("usage: gui-forms-facade-generator <catalogue.json> <windows-ref-dir> <output-dir>");
    return 2;
}

var cataloguePath = Path.GetFullPath(args[0]);
var windowsRef = Path.GetFullPath(args[1]);
var output = Path.GetFullPath(args[2]);
var coreRef = Directory.GetDirectories(Path.GetFullPath(Path.Combine(Path.GetDirectoryName(typeof(object).Assembly.Location)!, "..", "..", "..", "packs", "Microsoft.NETCore.App.Ref")))
    .OrderByDescending(value => value, StringComparer.Ordinal).Select(value => Path.Combine(value, "ref", "net10.0"))
    .First(Directory.Exists);
var resolverPaths = Directory.GetFiles(coreRef, "*.dll").Concat(Directory.GetFiles(windowsRef, "*.dll"))
    .Distinct(StringComparer.Ordinal).OrderBy(value => value, StringComparer.Ordinal).ToArray();
using var context = new MetadataLoadContext(new PathAssemblyResolver(resolverPaths), "System.Runtime");
var assemblies = new Dictionary<string, Assembly>(StringComparer.Ordinal)
{
    ["System.Windows.Forms"] = context.LoadFromAssemblyPath(Path.Combine(windowsRef, "System.Windows.Forms.dll")),
    ["System.Windows.Forms.Primitives"] = context.LoadFromAssemblyPath(Path.Combine(windowsRef, "System.Windows.Forms.Primitives.dll")),
    ["System.Drawing.Common"] = context.LoadFromAssemblyPath(Path.Combine(windowsRef, "System.Drawing.Common.dll")),
    ["System.Drawing.Primitives"] = context.LoadFromAssemblyPath(Path.Combine(coreRef, "System.Drawing.Primitives.dll")),
};

using var document = JsonDocument.Parse(File.ReadAllBytes(cataloguePath));
var rows = document.RootElement.GetProperty("apiRows").EnumerateArray()
    .Where(row => row.GetProperty("disposition").GetString() == "required" &&
                  row.GetProperty("implementationOwner").GetString() is
                      "gui_forms_managed_facade" or "gui_drawing_compat_facade")
    .Select(ApiRow.Read).OrderBy(row => row.Assembly, StringComparer.Ordinal)
    .ThenBy(row => row.Type, StringComparer.Ordinal).ThenBy(row => row.Member, StringComparer.Ordinal)
    .ThenBy(row => row.Signature, StringComparer.Ordinal).ToArray();

var selected = new Dictionary<Type, HashSet<MemberInfo>>();
var resolutions = new List<Resolution>();
var forwards = new Dictionary<string, HashSet<Type>>(StringComparer.Ordinal)
{
    ["System.Windows.Forms"] = [], ["System.Windows.Forms.Primitives"] = [],
    ["System.Drawing.Common"] = [], ["System.Drawing.Primitives"] = []
};

foreach (var row in rows)
{
    var type = assemblies[row.Assembly].GetType(row.Type, throwOnError: false, ignoreCase: false);
    if (type is null)
    {
        resolutions.Add(new(row.Id, row.Assembly, row.Type, row.Member, row.Signature, "unresolved_type", null));
        continue;
    }
    var actualAssembly = type.Assembly.GetName().Name!;
    if (actualAssembly != row.Assembly && forwards.ContainsKey(row.Assembly))
        forwards[row.Assembly].Add(type);
    AddType(type);
    if (row.MemberKind == "type")
    {
        resolutions.Add(new(row.Id, row.Assembly, row.Type, null, null, "resolved_type", actualAssembly));
        continue;
    }
    var member = ResolveMember(type, row);
    if (member is null)
    {
        resolutions.Add(new(row.Id, row.Assembly, row.Type, row.Member, row.Signature, "unresolved_member", actualAssembly));
        continue;
    }
    selected[type].Add(member);
    if (member is MethodInfo method)
    {
        AddSignatureType(method.ReturnType);
        foreach (var parameter in method.GetParameters()) AddSignatureType(parameter.ParameterType);
    }
    else if (member is ConstructorInfo constructor)
        foreach (var parameter in constructor.GetParameters()) AddSignatureType(parameter.ParameterType);
    else if (member is FieldInfo field) AddSignatureType(field.FieldType);
    resolutions.Add(new(row.Id, row.Assembly, row.Type, row.Member, row.Signature, "resolved_member", actualAssembly));
}

// Forms' first-party paint projection uses PNG explicitly even when the
// specimen catalogue only reaches ImageFormat indirectly through Image.Save.
var imageFormatType = assemblies["System.Drawing.Common"].GetType(
    "System.Drawing.Imaging.ImageFormat", throwOnError: true)!;
AddType(imageFormatType);
selected[imageFormatType].Add(imageFormatType.GetProperty("Png")!.GetMethod!);
var imageType = assemblies["System.Drawing.Common"].GetType(
    "System.Drawing.Image", throwOnError: true)!;
AddType(imageType);
selected[imageType].Add(imageType.GetMethods(BindingFlags.Public | BindingFlags.Instance)
    .Single(method => method.Name == "Save" && method.GetParameters() is var parameters &&
                      parameters.Length == 2 &&
                      parameters[0].ParameterType.FullName == "System.IO.Stream" &&
                      parameters[1].ParameterType.FullName == "System.Drawing.Imaging.ImageFormat"));

// The captured workload is a floor, not the nominal Forms contract. Owner-paint
// buffering cannot be implemented coherently when only the specimen-observed
// setter is emitted: subclasses must be able to read the requested state, query
// styles, and participate in the ordinary background/foreground paint phases.
var controlType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.Control", throwOnError: true)!;
var controlStylesType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.ControlStyles", throwOnError: true)!;
var paintEventArgsType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.PaintEventArgs", throwOnError: true)!;
var doubleBufferedProperty = controlType.GetProperty(
    "DoubleBuffered", BindingFlags.NonPublic | BindingFlags.Instance)!;
SelectNominalMember(doubleBufferedProperty.GetMethod!);
SelectNominalMember(doubleBufferedProperty.SetMethod!);
SelectNominalMember(controlType.GetMethod(
    "GetStyle", BindingFlags.NonPublic | BindingFlags.Instance, null,
    [controlStylesType], null)!);
SelectNominalMember(controlType.GetMethod(
    "OnPaintBackground", BindingFlags.NonPublic | BindingFlags.Instance, null,
    [paintEventArgsType], null)!);
SelectNominalMember(controlType.GetMethod(
    "InvokePaintBackground", BindingFlags.NonPublic | BindingFlags.Instance, null,
    [controlType, paintEventArgsType], null)!);

// Damage is part of the paint contract, not an optional specimen detail. Emit
// every bounded invalidation entry point plus its notification hooks so managed
// subclasses can observe the same rectangle that the retained surface merges.
var rectangleType = assemblies["System.Drawing.Primitives"].GetType(
    "System.Drawing.Rectangle", throwOnError: true)!;
var regionType = assemblies["System.Drawing.Common"].GetType(
    "System.Drawing.Region", throwOnError: true)!;
foreach (var method in controlType.GetMethods(BindingFlags.Public | BindingFlags.Instance)
             .Where(method => method.Name == "Invalidate"))
    SelectNominalMember(method);
SelectNominalMember(controlType.GetMethod(
    "NotifyInvalidate", BindingFlags.NonPublic | BindingFlags.Instance, null,
    [rectangleType], null)!);
var invalidateEventArgsType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.InvalidateEventArgs", throwOnError: true)!;
SelectNominalMember(controlType.GetMethod(
    "OnInvalidated", BindingFlags.NonPublic | BindingFlags.Instance, null,
    [invalidateEventArgsType], null)!);
var invalidatedEvent = controlType.GetEvent(
    "Invalidated", BindingFlags.Public | BindingFlags.Instance)!;
SelectNominalMember(invalidatedEvent.AddMethod!);
SelectNominalMember(invalidatedEvent.RemoveMethod!);
SelectNominalConstructor(invalidateEventArgsType.GetConstructor([rectangleType])!);
SelectNominalMember(invalidateEventArgsType.GetProperty("InvalidRect")!.GetMethod!);
SelectNominalMember(regionType.GetMethod(
    "GetBounds", BindingFlags.Public | BindingFlags.Instance, null,
    [assemblies["System.Drawing.Common"].GetType(
        "System.Drawing.Graphics", throwOnError: true)!], null)!);

// Geometry, ordering, and preferred-size queries are foundational Control
// behavior, not workload accidents. Emit the complete bounded family so a
// generated consumer never has to replace retained layout with private helper
// code merely because the captured specimen missed an overload.
foreach (var propertyName in new[] {
             "AllowDrop", "AutoSize", "Bounds", "ClientRectangle", "ClientSize",
             "ContainsFocus", "Height", "Left", "Location", "Margin",
             "MaximumSize", "MinimumSize", "PreferredSize", "Right", "Size",
             "TabIndex", "TabStop", "Top", "Width" })
{
    var property = controlType.GetProperty(
        propertyName, BindingFlags.Public | BindingFlags.Instance)!;
    if (property.GetMethod is not null) SelectNominalMember(property.GetMethod);
    if (property.SetMethod is not null) SelectNominalMember(property.SetMethod);
}
foreach (var eventName in new[] { "AutoSizeChanged", "LocationChanged", "SizeChanged" })
{
    var eventInfo = controlType.GetEvent(
        eventName, BindingFlags.Public | BindingFlags.Instance)!;
    SelectNominalMember(eventInfo.AddMethod!);
    SelectNominalMember(eventInfo.RemoveMethod!);
}
foreach (var methodName in new[] {
             "BringToFront", "Contains", "GetChildAtPoint", "GetNextControl",
             "GetPreferredSize", "PointToClient", "PointToScreen",
             "RectangleToClient", "RectangleToScreen", "SendToBack",
             "SetBounds", "SuspendLayout", "ResumeLayout", "PerformLayout" })
{
    foreach (var method in controlType.GetMethods(
                 BindingFlags.Public | BindingFlags.Instance)
             .Where(method => method.Name == methodName))
        SelectNominalMember(method);
}

var layoutEventArgsType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.LayoutEventArgs", throwOnError: true)!;
foreach (var constructor in layoutEventArgsType.GetConstructors(
             BindingFlags.Public | BindingFlags.Instance))
    SelectNominalConstructor(constructor);
foreach (var propertyName in new[] {
             "AffectedComponent", "AffectedControl", "AffectedProperty" })
    SelectNominalMember(layoutEventArgsType.GetProperty(
        propertyName, BindingFlags.Public | BindingFlags.Instance)!.GetMethod!);
foreach (var methodName in new[] {
             "GetAutoSizeMode", "SetAutoSizeMode", "SetBoundsCore" })
{
    foreach (var method in controlType.GetMethods(
                 BindingFlags.NonPublic | BindingFlags.Instance)
             .Where(method => method.Name == methodName))
        SelectNominalMember(method);
}

// Scrolling is a retained layout substrate, not a specimen-specific visual.
// Select the bounded Forms family together so panels, derived controls, and
// accessibility clients see one coherent two-axis contract.
var autoScrollOffsetProperty = controlType.GetProperty(
    "AutoScrollOffset", BindingFlags.Public | BindingFlags.Instance)!;
SelectNominalMember(autoScrollOffsetProperty.GetMethod!);
SelectNominalMember(autoScrollOffsetProperty.SetMethod!);

var scrollableControlType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.ScrollableControl", throwOnError: true)!;
foreach (var propertyName in new[] {
             "AutoScroll", "AutoScrollMargin", "AutoScrollMinSize",
             "AutoScrollPosition", "DisplayRectangle", "HorizontalScroll",
             "VerticalScroll", "HScroll", "VScroll" })
{
    var property = scrollableControlType.GetProperty(
        propertyName, BindingFlags.Public | BindingFlags.NonPublic |
                      BindingFlags.Instance)!;
    if (property.GetMethod is not null) SelectNominalMember(property.GetMethod);
    if (property.SetMethod is not null) SelectNominalMember(property.SetMethod);
}
var scrollEvent = scrollableControlType.GetEvent(
    "Scroll", BindingFlags.Public | BindingFlags.Instance)!;
SelectNominalMember(scrollEvent.AddMethod!);
SelectNominalMember(scrollEvent.RemoveMethod!);
foreach (var methodName in new[] {
             "ScrollControlIntoView", "SetAutoScrollMargin",
             "SetDisplayRectLocation", "GetScrollState", "SetScrollState",
             "AdjustFormScrollbars", "ScrollToControl", "OnScroll",
             "OnLayout", "OnMouseWheel" })
{
    foreach (var method in scrollableControlType.GetMethods(
                 BindingFlags.Public | BindingFlags.NonPublic |
                 BindingFlags.Instance)
             .Where(method => method.Name == methodName))
        SelectNominalMember(method);
}
foreach (var fieldName in new[] {
             "ScrollStateAutoScrolling", "ScrollStateHScrollVisible",
             "ScrollStateVScrollVisible", "ScrollStateUserHasScrolled",
             "ScrollStateFullDrag" })
    selected[scrollableControlType].Add(scrollableControlType.GetField(
        fieldName, BindingFlags.NonPublic | BindingFlags.Static)!);

var scrollPropertiesType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.ScrollProperties", throwOnError: true)!;
AddType(scrollPropertiesType);
foreach (var propertyName in new[] {
             "Enabled", "LargeChange", "Maximum", "Minimum",
             "ParentControl", "SmallChange", "Value", "Visible" })
{
    var property = scrollPropertiesType.GetProperty(
        propertyName, BindingFlags.Public | BindingFlags.NonPublic |
                      BindingFlags.Instance)!;
    if (property.GetMethod is not null) SelectNominalMember(property.GetMethod);
    if (property.SetMethod is not null) SelectNominalMember(property.SetMethod);
}
AddType(assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.HScrollProperties", throwOnError: true)!);
AddType(assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.VScrollProperties", throwOnError: true)!);

// PropertyGrid is a reusable native tooling surface even when the compatibility
// specimen does not instantiate it. Emit the bounded runtime family explicitly;
// design-time tabs, command services, and arbitrary component editors remain
// outside this projection.
var propertyGridType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.PropertyGrid", throwOnError: true)!;
SelectNominalConstructor(propertyGridType.GetConstructor(Type.EmptyTypes)!);
foreach (var propertyName in new[] {
             "SelectedObject", "SelectedObjects", "PropertySort" })
{
    var property = propertyGridType.GetProperty(
        propertyName, BindingFlags.Public | BindingFlags.Instance)!;
    if (property.GetMethod is not null) SelectNominalMember(property.GetMethod);
    if (property.SetMethod is not null) SelectNominalMember(property.SetMethod);
}
foreach (var eventName in new[] { "SelectedObjectsChanged", "PropertySortChanged" })
{
    var eventInfo = propertyGridType.GetEvent(
        eventName, BindingFlags.Public | BindingFlags.Instance)!;
    SelectNominalMember(eventInfo.AddMethod!);
    SelectNominalMember(eventInfo.RemoveMethod!);
}
SelectNominalMember(propertyGridType.GetMethod(
    "Refresh", BindingFlags.Public | BindingFlags.Instance, null,
    Type.EmptyTypes, null)!);

// Property descriptors may name managed editors even when the compatibility
// specimen does not. Emit the standard runtime editor vocabulary as part of
// System.Windows.Forms (its reference-assembly owner in .NET 10) so ordinary
// EditorAttribute declarations and editor-service lookups compile unchanged.
var uiTypeEditorType = assemblies["System.Windows.Forms"].GetType(
    "System.Drawing.Design.UITypeEditor", throwOnError: true)!;
foreach (var constructor in uiTypeEditorType.GetConstructors(
             BindingFlags.Public | BindingFlags.Instance))
    SelectNominalConstructor(constructor);
foreach (var method in uiTypeEditorType.GetMethods(
             BindingFlags.Public | BindingFlags.Instance |
             BindingFlags.DeclaredOnly))
    SelectNominalMember(method);
AddType(assemblies["System.Windows.Forms"].GetType(
    "System.Drawing.Design.UITypeEditorEditStyle", throwOnError: true)!);
var paintValueEventArgsType = assemblies["System.Windows.Forms"].GetType(
    "System.Drawing.Design.PaintValueEventArgs", throwOnError: true)!;
SelectNominalConstructor(paintValueEventArgsType.GetConstructors(
    BindingFlags.Public | BindingFlags.Instance).Single());
foreach (var propertyName in new[] { "Bounds", "Context", "Graphics", "Value" })
    SelectNominalMember(paintValueEventArgsType.GetProperty(
        propertyName, BindingFlags.Public | BindingFlags.Instance)!.GetMethod!);
var editorServiceType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.Design.IWindowsFormsEditorService",
    throwOnError: true)!;
AddType(editorServiceType);
foreach (var method in editorServiceType.GetMethods(
             BindingFlags.Public | BindingFlags.Instance |
             BindingFlags.DeclaredOnly))
    SelectNominalMember(method);

var scrollEventArgsType = assemblies["System.Windows.Forms"].GetType(
    "System.Windows.Forms.ScrollEventArgs", throwOnError: true)!;
foreach (var constructor in scrollEventArgsType.GetConstructors(
             BindingFlags.Public | BindingFlags.Instance))
    SelectNominalConstructor(constructor);
foreach (var propertyName in new[] {
             "Type", "OldValue", "NewValue", "ScrollOrientation" })
{
    var property = scrollEventArgsType.GetProperty(propertyName)!;
    if (property.GetMethod is not null) SelectNominalMember(property.GetMethod);
    if (property.SetMethod is not null) SelectNominalMember(property.SetMethod);
}
var controlCollectionType = controlType.GetNestedType(
    "ControlCollection", BindingFlags.Public)!;
foreach (var methodName in new[] {
             "Contains", "GetChildIndex", "IndexOf", "SetChildIndex" })
{
    foreach (var method in controlCollectionType.GetMethods(
                 BindingFlags.Public | BindingFlags.Instance)
             .Where(method => method.Name == methodName))
        SelectNominalMember(method);
}

// .resources stores legacy Bitmap values with the ActivatorStream encoding.
// This constructor is reached reflectively by System.Resources.Extensions and
// therefore cannot appear as a static IL operand in the specimen catalogue.
var bitmapType = assemblies["System.Drawing.Common"].GetType(
    "System.Drawing.Bitmap", throwOnError: true)!;
AddType(bitmapType);
selected[bitmapType].Add(bitmapType.GetConstructors(BindingFlags.Public | BindingFlags.Instance)
    .Single(constructor => constructor.GetParameters() is var parameters &&
                           parameters.Length == 1 &&
                           parameters[0].ParameterType.FullName == "System.IO.Stream"));
var iconType = assemblies["System.Drawing.Common"].GetType(
    "System.Drawing.Icon", throwOnError: true)!;
AddType(iconType);
selected[iconType].Add(iconType.GetConstructors(BindingFlags.Public | BindingFlags.Instance)
    .Single(constructor => constructor.GetParameters() is var parameters &&
                           parameters.Length == 1 &&
                           parameters[0].ParameterType.FullName == "System.IO.Stream"));

void AddSignatureType(Type type)
{
    while (type.HasElementType) type = type.GetElementType()!;
    if (type.IsGenericType)
        foreach (var argument in type.GetGenericArguments()) AddSignatureType(argument);
    if (assemblies.ContainsKey(type.Assembly.GetName().Name!)) AddType(type);
}

void SelectNominalMember(MethodInfo method)
{
    AddType(method.DeclaringType!);
    selected[method.DeclaringType!].Add(method);
    AddSignatureType(method.ReturnType);
    foreach (var parameter in method.GetParameters()) AddSignatureType(parameter.ParameterType);
}

void SelectNominalConstructor(ConstructorInfo constructor)
{
    AddType(constructor.DeclaringType!);
    selected[constructor.DeclaringType!].Add(constructor);
    foreach (var parameter in constructor.GetParameters())
        AddSignatureType(parameter.ParameterType);
}

void AddType(Type type)
{
    if (type.IsGenericType && !type.IsGenericTypeDefinition) type = type.GetGenericTypeDefinition();
    if (!selected.TryAdd(type, [])) return;
    if (type.DeclaringType is not null) AddType(type.DeclaringType);
    if (type.BaseType is { } baseType && assemblies.ContainsKey(baseType.Assembly.GetName().Name!))
        AddType(baseType);
    if (type.IsEnum)
        foreach (var field in type.GetFields(BindingFlags.Public | BindingFlags.Static)) selected[type].Add(field);
    if (type.BaseType?.FullName == "System.MulticastDelegate" && type.GetMethod("Invoke") is { } invoke)
    {
        selected[type].Add(invoke);
        AddSignatureType(invoke.ReturnType);
        foreach (var parameter in invoke.GetParameters()) AddSignatureType(parameter.ParameterType);
    }
    if (type.Assembly.GetName().Name == "System.Drawing.Common" &&
        type.GetInterfaces().Any(value => value.FullName == "System.IDisposable"))
    {
        var dispose = type.GetMethod("Dispose", BindingFlags.Public | BindingFlags.Instance,
                                     binder: null, Type.EmptyTypes, modifiers: null);
        if (dispose is not null && dispose.DeclaringType == type) selected[type].Add(dispose);
    }
}

Directory.CreateDirectory(output);
foreach (var assemblyName in assemblies.Keys.Where(name => name != "System.Drawing.Primitives"))
{
    var directory = Path.Combine(output, assemblyName);
    Directory.CreateDirectory(directory);
    var owned = selected.Keys.Where(type => type.Assembly.GetName().Name == assemblyName).ToHashSet();
    File.WriteAllText(Path.Combine(directory, "Surface.g.cs"), EmitAssembly(assemblyName, owned, selected), new UTF8Encoding(false));
    File.WriteAllText(Path.Combine(directory, assemblyName + ".csproj"), ProjectFile(assemblyName), new UTF8Encoding(false));
    if (assemblyName == "System.Windows.Forms")
        File.WriteAllText(Path.Combine(directory, "NativeBridge.g.cs"), NativeBridgeSource(), new UTF8Encoding(false));
    if (assemblyName == "System.Drawing.Common")
        File.WriteAllText(Path.Combine(directory, "DrawingBridge.g.cs"), DrawingBridgeSource(), new UTF8Encoding(false));
    if (forwards[assemblyName].Count != 0)
    {
        var source = new StringBuilder("using System.Runtime.CompilerServices;\n");
        foreach (var type in forwards[assemblyName].OrderBy(value => value.FullName, StringComparer.Ordinal))
            source.Append("[assembly: TypeForwardedTo(typeof(").Append(CsType(type)).AppendLine("))]");
        File.WriteAllText(Path.Combine(directory, "Forwards.g.cs"), source.ToString(), new UTF8Encoding(false));
    }
}

var manifest = new
{
    schema = "gui.forms.generated-surface/v1",
    toolVersion = "0.1.0",
    catalogue = "retired-compatibility-specimen.facade-catalogue-v1.json",
    requiredRowCount = rows.Length,
    resolvedRowCount = resolutions.Count(value => value.Status.StartsWith("resolved_", StringComparison.Ordinal)),
    unresolvedRowCount = resolutions.Count(value => value.Status.StartsWith("unresolved_", StringComparison.Ordinal)),
    emittedTypeCount = selected.Count,
    rows = resolutions,
};
File.WriteAllText(Path.Combine(output, "surface-manifest-v1.json"),
    JsonSerializer.Serialize(manifest, new JsonSerializerOptions { WriteIndented = true }) + "\n", new UTF8Encoding(false));
Console.WriteLine($"surface: {manifest.resolvedRowCount}/{manifest.requiredRowCount} rows resolved; {manifest.emittedTypeCount} types emitted");
return manifest.unresolvedRowCount == 0 ? 0 : 1;

static MemberInfo? ResolveMember(Type type, ApiRow row)
{
    const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly;
    IEnumerable<MemberInfo> candidates = row.MemberKind == "field"
        ? type.GetFields(flags).Where(field => field.Name == row.Member)
        : row.Member == ".ctor"
            ? type.GetConstructors(flags)
            : type.GetMethods(flags).Where(method => method.Name == row.Member);
    return candidates.FirstOrDefault(member => MemberSignature(member) == row.Signature);
}

static string MemberSignature(MemberInfo member) => member switch
{
    FieldInfo field => MetadataType(field.FieldType),
    MethodBase method => (method is MethodInfo info ? MetadataType(info.ReturnType) : "System.Void") + " " +
        (method.IsGenericMethodDefinition ? "``" + method.GetGenericArguments().Length : "") + "(" +
        string.Join(",", method.GetParameters().Select(parameter => MetadataType(parameter.ParameterType))) + ")",
    _ => throw new NotSupportedException(member.MemberType.ToString()),
};

static string MetadataType(Type type)
{
    if (type.IsByRef) return MetadataType(type.GetElementType()!) + "&";
    if (type.IsPointer) return MetadataType(type.GetElementType()!) + "*";
    if (type.IsArray) return MetadataType(type.GetElementType()!) + "[" + new string(',', type.GetArrayRank() - 1) + "]";
    if (type.IsGenericParameter) return (type.DeclaringMethod is null ? "!" : "!!") + type.GenericParameterPosition;
    if (type.IsGenericType)
        return type.GetGenericTypeDefinition().FullName! + "<" + string.Join(",", type.GetGenericArguments().Select(MetadataType)) + ">";
    return type.FullName ?? type.Name;
}

static string EmitAssembly(string assemblyName, HashSet<Type> owned, Dictionary<Type, HashSet<MemberInfo>> selected)
{
    var result = new StringBuilder("// <auto-generated/>\n#nullable enable\n#pragma warning disable 0067,0108,0109,0114,0169,0649,8600,8601,8602,8603,8618,8625\n");
    var telemetryKeys = owned.Where(type => !type.IsInterface)
        .SelectMany(type => selected[type].Select(member => (Type: type, Member: member)))
        .Where(entry => entry.Member is ConstructorInfo || entry.Member is MethodInfo method &&
            (!method.IsSpecialName || method.Name.StartsWith("get_", StringComparison.Ordinal) ||
             method.Name.StartsWith("set_", StringComparison.Ordinal)))
        .Select(entry => TelemetryKey(entry.Type, (MethodBase)entry.Member))
        .Distinct(StringComparer.Ordinal).OrderBy(value => value, StringComparer.Ordinal).ToArray();
    foreach (var group in owned.Where(type => type.DeclaringType is null).OrderBy(type => type.Namespace, StringComparer.Ordinal)
                 .ThenBy(type => type.Name, StringComparer.Ordinal).GroupBy(type => type.Namespace ?? ""))
    {
        if (group.Key.Length != 0) result.Append("namespace ").Append(group.Key).AppendLine(" {");
        foreach (var type in group) EmitType(result, type, owned, selected, group.Key.Length == 0 ? 0 : 1);
        if (group.Key.Length != 0) result.AppendLine("}");
    }
    if (assemblyName == "System.Windows.Forms")
    {
        result.AppendLine("namespace System.Windows.Forms {");
        EmitTelemetryClass(result, assemblyName, telemetryKeys, 1);
        result.AppendLine("    internal static class FacadeStubDiagnostics {");
        result.AppendLine("        private static readonly bool Trace = global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_STUBS\") == \"1\";");
        result.AppendLine("        internal static T Value<T>(string member) { if (Trace) global::System.Console.Error.WriteLine(\"facade-stub=\" + member); return default!; }");
        result.AppendLine("    }");
        result.AppendLine("    public class ApplicationContext : global::System.IDisposable {");
        result.AppendLine("        public ApplicationContext() { }");
        result.AppendLine("        public ApplicationContext(Form mainForm) { MainForm = mainForm ?? throw new global::System.ArgumentNullException(nameof(mainForm)); }");
        result.AppendLine("        public Form? MainForm { get; set; }");
        result.AppendLine("        public event global::System.EventHandler? ThreadExit;");
        result.AppendLine("        public void ExitThread() { MainForm?.Close(); }");
        result.AppendLine("        protected virtual void OnThreadExit(global::System.EventArgs e) { ThreadExit?.Invoke(this, e); }");
        result.AppendLine("        internal void __NotifyThreadExit() { OnThreadExit(global::System.EventArgs.Empty); }");
        result.AppendLine("        public void Dispose() { MainForm = null; global::System.GC.SuppressFinalize(this); }");
        result.AppendLine("    }");
        result.AppendLine("}");
    }
    else if (assemblyName == "System.Windows.Forms.Primitives")
    {
        result.AppendLine("namespace System.Windows.Forms {");
        EmitTelemetryClass(result, assemblyName, telemetryKeys, 1);
        result.AppendLine("    internal static class FacadeStubDiagnostics {");
        result.AppendLine("        private static readonly bool Trace = global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_STUBS\") == \"1\";");
        result.AppendLine("        internal static T Value<T>(string member) { if (Trace) global::System.Console.Error.WriteLine(\"facade-stub=\" + member); return default!; }");
        result.AppendLine("    }");
        result.AppendLine("}");
    }
    else
    {
        result.AppendLine("namespace System.Drawing {");
        EmitTelemetryClass(result, assemblyName, telemetryKeys, 1);
        result.AppendLine("    internal static class FacadeStubDiagnostics {");
        result.AppendLine("        private static readonly bool Trace = global::System.Environment.GetEnvironmentVariable(\"GUI_DRAWING_TRACE_STUBS\") == \"1\";");
        result.AppendLine("        internal static T Value<T>(string member) { if (Trace) global::System.Console.Error.WriteLine(\"drawing-facade-stub=\" + member); return default!; }");
        result.AppendLine("    }");
        result.AppendLine("    public sealed class ImageConverter : global::System.ComponentModel.TypeConverter {");
        result.AppendLine("        public override bool CanConvertFrom(global::System.ComponentModel.ITypeDescriptorContext? context, global::System.Type sourceType) => sourceType == typeof(byte[]) || base.CanConvertFrom(context, sourceType);");
        result.AppendLine("        public override object? ConvertFrom(global::System.ComponentModel.ITypeDescriptorContext? context, global::System.Globalization.CultureInfo? culture, object value) { if (value is byte[] bytes) return NativeDrawingBridge.DecodePng(bytes); return base.ConvertFrom(context, culture, value); }");
        result.AppendLine("    }");
        result.AppendLine("}");
    }
    return result.ToString();
}

static void EmitTelemetryClass(StringBuilder output, string assemblyName,
                               IReadOnlyList<string> keys, int indent)
{
    var pad = new string(' ', indent * 4);
    output.Append(pad).AppendLine("internal static class FacadeCallTelemetry {");
    output.Append(pad).Append("    private const string AssemblyName = \"")
        .Append(CsString(assemblyName)).AppendLine("\";");
    output.Append(pad).AppendLine("    private static readonly string? DirectoryPath = global::System.Environment.GetEnvironmentVariable(\"GUI_FACADE_CALL_REPORT_DIR\");");
    output.Append(pad).AppendLine("    private static readonly bool Enabled = !global::System.String.IsNullOrWhiteSpace(DirectoryPath);");
    output.Append(pad).AppendLine("    private static readonly global::System.Collections.Concurrent.ConcurrentDictionary<string, long> Counts = new(global::System.StringComparer.Ordinal);");
    output.Append(pad).AppendLine("    private static readonly object FlushGate = new();");
    output.Append(pad).AppendLine("    private static readonly string[] AllKeys = new string[] {");
    foreach (var key in keys)
        output.Append(pad).Append("        \"").Append(CsString(key)).AppendLine("\",");
    output.Append(pad).AppendLine("    };");
    output.Append(pad).AppendLine("    private static readonly global::System.Threading.Timer? FlushTimer = Enabled ? new global::System.Threading.Timer(_ => Flush(), null, 2000, 2000) : null;");
    output.Append(pad).AppendLine("    static FacadeCallTelemetry() { if (Enabled) global::System.AppDomain.CurrentDomain.ProcessExit += (_, _) => Flush(); }");
    output.Append(pad).AppendLine("    [global::System.Runtime.CompilerServices.MethodImpl(global::System.Runtime.CompilerServices.MethodImplOptions.AggressiveInlining)]");
    output.Append(pad).AppendLine("    internal static void Hit(string key) { if (!Enabled) return; Counts.AddOrUpdate(key, 1, static (_, value) => value + 1); }");
    output.Append(pad).AppendLine("    internal static bool IsEnabled { get { return Enabled; } }");
    output.Append(pad).AppendLine("    internal static void Observe(string category, string value) { if (!Enabled) return; Hit(\"metric|\" + category + \"|\" + value); }");
    output.Append(pad).AppendLine("    internal static void ObserveValue(string category, long value) { if (!Enabled) return; Counts.AddOrUpdate(\"metric|\" + category + \"|samples\", 1, static (_, prior) => prior + 1); Counts.AddOrUpdate(\"metric|\" + category + \"|sum\", value, (_, prior) => checked(prior + value)); Counts.AddOrUpdate(\"metric|\" + category + \"|min\", value, (_, prior) => global::System.Math.Min(prior, value)); Counts.AddOrUpdate(\"metric|\" + category + \"|max\", value, (_, prior) => global::System.Math.Max(prior, value)); }");
    output.Append(pad).AppendLine("    internal static void Flush() {");
    output.Append(pad).AppendLine("        if (!Enabled || DirectoryPath is null) return;");
    output.Append(pad).AppendLine("        lock (FlushGate) {");
    output.Append(pad).AppendLine("            try {");
    output.Append(pad).AppendLine("                global::System.IO.Directory.CreateDirectory(DirectoryPath);");
    output.Append(pad).AppendLine("                var lines = new global::System.Collections.Generic.List<string>(AllKeys.Length + 5);");
    output.Append(pad).AppendLine("                lines.Add(\"schema\\tgui.forms.call-coverage/v1\");");
    output.Append(pad).AppendLine("                lines.Add(\"assembly\\t\" + AssemblyName);");
    output.Append(pad).AppendLine("                lines.Add(\"process_id\\t\" + global::System.Environment.ProcessId.ToString(global::System.Globalization.CultureInfo.InvariantCulture));");
    output.Append(pad).AppendLine("                lines.Add(\"instrumented_keys\\t\" + AllKeys.Length.ToString(global::System.Globalization.CultureInfo.InvariantCulture));");
    output.Append(pad).AppendLine("                lines.Add(\"key\\tcount\");");
    output.Append(pad).AppendLine("                foreach (var key in AllKeys) { Counts.TryGetValue(key, out var count); lines.Add(key + \"\\t\" + count.ToString(global::System.Globalization.CultureInfo.InvariantCulture)); }");
    output.Append(pad).AppendLine("                var known = new global::System.Collections.Generic.HashSet<string>(AllKeys, global::System.StringComparer.Ordinal);");
    output.Append(pad).AppendLine("                var metrics = new global::System.Collections.Generic.List<global::System.Collections.Generic.KeyValuePair<string, long>>();");
    output.Append(pad).AppendLine("                foreach (var entry in Counts) if (!known.Contains(entry.Key)) metrics.Add(entry);");
    output.Append(pad).AppendLine("                metrics.Sort((left, right) => global::System.StringComparer.Ordinal.Compare(left.Key, right.Key));");
    output.Append(pad).AppendLine("                foreach (var entry in metrics) lines.Add(entry.Key + \"\\t\" + entry.Value.ToString(global::System.Globalization.CultureInfo.InvariantCulture));");
    output.Append(pad).AppendLine("                var process = global::System.Environment.ProcessId.ToString(global::System.Globalization.CultureInfo.InvariantCulture);");
    output.Append(pad).AppendLine("                var path = global::System.IO.Path.Combine(DirectoryPath, AssemblyName + \".\" + process + \".calls.tsv\");");
    output.Append(pad).AppendLine("                var temporary = path + \".tmp\";");
    output.Append(pad).AppendLine("                global::System.IO.File.WriteAllLines(temporary, lines);");
    output.Append(pad).AppendLine("                global::System.IO.File.Move(temporary, path, true);");
    output.Append(pad).AppendLine("            } catch (global::System.Exception error) {");
    output.Append(pad).AppendLine("                if (global::System.Environment.GetEnvironmentVariable(\"GUI_FACADE_CALL_REPORT_TRACE\") == \"1\") global::System.Console.Error.WriteLine(\"facade-call-report-error=assembly:\" + AssemblyName + \"|type:\" + error.GetType().FullName + \"|message:\" + error.Message.Replace('\\r', ' ').Replace('\\n', ' '));");
    output.Append(pad).AppendLine("            }");
    output.Append(pad).AppendLine("        }");
    output.Append(pad).AppendLine("    }");
    output.Append(pad).AppendLine("}");
}

static string CsString(string value) => value.Replace("\\", "\\\\", StringComparison.Ordinal)
    .Replace("\"", "\\\"", StringComparison.Ordinal);

static string TelemetryKey(Type type, MethodBase member) =>
    type.Assembly.GetName().Name + "|" + type.FullName + "|" +
    (member is ConstructorInfo ? ".ctor" : member.Name) + "|" + MemberSignature(member);

static string TelemetryCall(Type type, MethodBase member) =>
    "global::" + (type.Assembly.GetName().Name == "System.Drawing.Common"
        ? "System.Drawing" : "System.Windows.Forms") +
    ".FacadeCallTelemetry.Hit(\"" + CsString(TelemetryKey(type, member)) + "\"); ";

static string InstrumentBody(string body, Type type, MethodBase member)
{
    var open = body.IndexOf('{');
    return open < 0 ? body : body.Insert(open + 1, " " + TelemetryCall(type, member));
}

static string InstrumentPropertyBody(string body, Type type, PropertyInfo property,
                                     HashSet<MethodInfo> accessors)
{
    foreach (var accessor in new[] { property.GetMethod, property.SetMethod })
    {
        if (accessor is null || !accessors.Contains(accessor)) continue;
        var token = accessor.Name.StartsWith("get_", StringComparison.Ordinal) ? "get {" : "set {";
        var index = body.IndexOf(token, StringComparison.Ordinal);
        if (index >= 0)
            body = body.Insert(index + token.Length, " " + TelemetryCall(type, accessor));
    }
    return body;
}

static void EmitType(StringBuilder output, Type type, HashSet<Type> owned,
                     Dictionary<Type, HashSet<MemberInfo>> selected, int indent)
{
    var pad = new string(' ', indent * 4);
    if (type.IsEnum)
    {
        output.Append(pad).Append("public enum ").Append(CleanName(type)).Append(" : ").Append(CsType(Enum.GetUnderlyingType(type))).AppendLine(" {");
        foreach (var field in selected[type].OfType<FieldInfo>().OrderBy(field => field.MetadataToken))
            output.Append(pad).Append("    ").Append(Escape(field.Name)).Append(" = ").Append(Convert.ToString(field.GetRawConstantValue(), System.Globalization.CultureInfo.InvariantCulture)).AppendLine(",");
        output.Append(pad).AppendLine("}");
        return;
    }
    if (type.BaseType?.FullName is "System.MulticastDelegate")
    {
        var invoke = selected[type].OfType<MethodInfo>().Single(method => method.Name == "Invoke");
        output.Append(pad).Append("public delegate ").Append(CsType(invoke.ReturnType)).Append(' ').Append(CleanName(type))
            .Append('(').Append(Parameters(invoke.GetParameters(), false)).AppendLine(");");
        return;
    }
    var isStatic = type.IsAbstract && type.IsSealed;
    var kind = type.IsInterface ? "interface" : type.IsValueType ? "struct" : "class";
    if (type.FullName is "System.Drawing.Image" or "System.Drawing.Bitmap")
        output.Append(pad).AppendLine("[global::System.ComponentModel.TypeConverter(typeof(global::System.Drawing.ImageConverter))]");
    output.Append(pad).Append("public ");
    if (isStatic) output.Append("static "); else if (type.IsAbstract && !type.IsInterface) output.Append("abstract ");
    output.Append("partial ").Append(kind).Append(' ').Append(CleanName(type));
    var hasBase = !type.IsInterface && !type.IsValueType && !isStatic &&
        type.BaseType is { } baseType && baseType != typeof(object);
    if (hasBase) output.Append(" : ").Append(CsType(type.BaseType!));
    if (!type.IsInterface && !type.IsValueType && !isStatic &&
        type.Assembly.GetName().Name == "System.Drawing.Common" &&
        type.GetInterfaces().Any(value => value.FullName == "System.IDisposable"))
        output.Append(hasBase ? ", " : " : ").Append("global::System.IDisposable");
    if (type.FullName == "System.Drawing.Graphics")
        output.Append(", global::System.Drawing.IDeviceContext");
    if (type.FullName == "System.Windows.Forms.Control")
        output.Append(hasBase ? ", " : " : ").Append("global::System.Windows.Forms.IWin32Window");
    if (type.FullName == "System.Windows.Forms.Button")
        output.Append(hasBase ? ", " : " : ").Append("global::System.Windows.Forms.IButtonControl");
    if (type.FullName is "System.Windows.Forms.UpDownBase" or
        "System.Windows.Forms.PictureBox" or
        "System.Windows.Forms.DataGridView" or
        "System.Windows.Forms.BindingSource")
        output.Append(", global::System.ComponentModel.ISupportInitialize");
    output.AppendLine(" {");

    if (type.FullName == "System.Windows.Forms.Control")
    {
        output.Append(pad).AppendLine("    internal NativeControlBridge __native = null!;");
        output.Append(pad).AppendLine("    protected ControlCollection __controls = null!;");
        output.Append(pad).AppendLine("    private void __NativeChanged(NativeChange change) { if (change == NativeChange.Text) { if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_TEXT\") == \"1\" && Text.Length != 0) global::System.Console.Error.WriteLine(\"facade-text=type:\" + GetType().FullName + \"|name:\" + Name + \"|text:\" + Text.Replace('\\r', ' ').Replace('\\n', ' ')); OnTextChanged(global::System.EventArgs.Empty); if (__autoSize) __RefreshAutoSizeFromChildren(); if (__loadRaised) __QueueManagedPaint(); } else if (change == NativeChange.Visible) OnVisibleChanged(global::System.EventArgs.Empty); else if (change == NativeChange.Enabled) OnEnabledChanged(global::System.EventArgs.Empty); else if (change == NativeChange.Bounds) { __InvalidateManagedPaintSurface(); OnSizeChanged(global::System.EventArgs.Empty); OnResize(global::System.EventArgs.Empty); ClientSizeChanged?.Invoke(this, global::System.EventArgs.Empty); PerformLayout(); __SynchronizeWindowSurfaceTree(); if (__loadRaised) __RenderWindowSurfaceTree(); } }");
        output.Append(pad).AppendLine("    internal virtual bool __NativeEvent(NativeEvent kind) { if (kind == NativeEvent.Clicked) Click?.Invoke(this, global::System.EventArgs.Empty); return false; }");
        output.Append(pad).AppendLine("    private void __NativePointer(NativePointer input) { if (__DeferManagedInput(() => __DeliverNativePointer(input))) return; __DeliverNativePointer(input); }");
        output.Append(pad).AppendLine("    private bool __managedLeftPointerDown;");
        output.Append(pad).AppendLine("    private void __DeliverNativePointer(NativePointer input) { var button = input.Button switch { 1u => MouseButtons.Left, 2u => MouseButtons.Right, 3u => MouseButtons.Middle, _ => MouseButtons.None }; if (button == MouseButtons.None && __managedLeftPointerDown && input.Kind is 5u or 7u) button = MouseButtons.Left; var e = new MouseEventArgs(button, input.Kind is 6u or 7u ? 1 : 0, (int)global::System.Math.Round(input.X), (int)global::System.Math.Round(input.Y), (int)global::System.Math.Round(input.WheelDelta)); __mousePosition = PointToScreen(new global::System.Drawing.Point(e.X, e.Y)); if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_CONTROLS\") == \"1\" && input.Kind is 6u or 7u) global::System.Console.Error.WriteLine($\"facade-pointer=type:{GetType().FullName}|name:{Name}|kind:{input.Kind}|x:{e.X}|y:{e.Y}|button:{button}\"); switch (input.Kind) { case 5u: OnMouseMove(e); break; case 6u: Application.__DismissActiveMenuForPointer(this); __managedLeftPointerDown = button == MouseButtons.Left; OnMouseDown(e); break; case 7u: var wasPressed = __managedLeftPointerDown; __managedLeftPointerDown = false; OnMouseUp(e); if (wasPressed && button == MouseButtons.Left && ClientRectangle.Contains(e.X, e.Y) && __GetStyle(ControlStyles.StandardClick) && !__NativePromotesPointerClick && this is not Button) __NativeEvent(NativeEvent.Clicked); break; case 8u: ScrollableControl? scroll = this as ScrollableControl; for (var ancestor = Parent; scroll is null && ancestor is not null; ancestor = ancestor.Parent) scroll = ancestor as ScrollableControl; if (scroll is null || !scroll.__ScrollByWheel(e.Delta)) OnMouseWheel(e); break; case 9u: OnMouseEnter(global::System.EventArgs.Empty); break; case 10u: OnMouseLeave(global::System.EventArgs.Empty); break; } }");
        output.Append(pad).AppendLine("    internal string __RunNativeWindow() { return __native.RunWindow(global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_AUTOMATION_CLOSE\") == \"1\", global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_FORCE_HEADLESS\") == \"1\", global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_AUTOMATION_ACTIVATE\") == \"1\", false); }");
        output.Append(pad).AppendLine("    internal string __RunNativePopup() { return __native.RunWindow(false, global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_FORCE_HEADLESS\") == \"1\", false, true); }");
        output.Append(pad).AppendLine("    internal void __DoEvents() { NativeControlBridge.DoEvents(global::System.Environment.CurrentManagedThreadId); }");
        output.Append(pad).AppendLine("    internal void __RequestClose() { __native.RequestClose(); }");
        output.Append(pad).AppendLine("    internal bool __ShowPathDialog(uint kind, string title, string initialDirectory, string suggestedName, string defaultExtension, string filter, uint flags, out string selectedPath) { return __native.ShowPathDialog(kind, title, initialDirectory, suggestedName, defaultExtension, filter, flags, out selectedPath); }");
        output.Append(pad).AppendLine("    internal void __ShowToolTip(string text, int x, int y, int duration) { __native.ShowToolTip(text, x, y, duration); }");
        output.Append(pad).AppendLine("    internal void __HideToolTip() { __native.HideToolTip(); }");
        output.Append(pad).AppendLine("    internal uint __NativeCheckState { get { return __native.CheckState; } set { __native.CheckState = value; } }");
        output.Append(pad).AppendLine("    internal double __NativeRangeValue { get { return __native.RangeValue; } set { __native.RangeValue = value; } }");
        output.Append(pad).AppendLine("    internal bool __NativePromotesPointerClick { get { return __native.PromotesPointerClick; } }");
        output.Append(pad).AppendLine("    internal nint __AcquireCompatibilityHandle() { var existed = __native.HasWindowHandle; var value = __native.WindowHandle; if (!existed && value != 0) { OnHandleCreated(global::System.EventArgs.Empty); __native.ConfigureWindowSurface(__HostOffset(), ClientSize); } return value; }");
        output.Append(pad).AppendLine("    internal string __WindowSurfaceSnapshot() { return __native.WindowSurfaceSnapshot(); }");
        output.Append(pad).AppendLine("    internal void __TouchWindowSurface() { __native.TouchWindowSurface(); }");
        output.Append(pad).AppendLine("    internal void __DrainWindowSurfaceNow() { __native.DrainWindowSurfaceNow(); }");
        output.Append(pad).AppendLine("    private void __ReleaseCompatibilityHandle() { if (__native.HasWindowHandle) OnHandleDestroyed(global::System.EventArgs.Empty); }");
        output.Append(pad).AppendLine("    protected virtual void OnHandleCreated(global::System.EventArgs e) { HandleCreated?.Invoke(this, e); }");
        output.Append(pad).AppendLine("    private bool __loadRaised;");
        output.Append(pad).AppendLine("    internal static readonly bool __TraceLifecycle = global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_LIFECYCLE\") == \"1\";");
        output.Append(pad).AppendLine("    internal static readonly bool __TraceInteraction = global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_INTERACTION\") == \"1\";");
        output.Append(pad).AppendLine("    internal bool __HasRaisedLoad { get { return __loadRaised; } }");
        output.Append(pad).AppendLine("    internal static Control? __FocusedControl { get { return __focusedControl; } }");
        output.Append(pad).AppendLine("    internal void __AttachKeyPreview(global::System.Func<NativeKey, bool> callback) { __native.KeyPreviewRaised += callback; }");
        output.Append(pad).AppendLine("    internal bool __BeginLoad() { if (__loadRaised) return false; __loadRaised = true; return true; }");
        output.Append(pad).AppendLine("    internal void __RaiseChildrenLoad() { foreach (Control child in Controls) child.__RaiseLoad(); }");
        output.Append(pad).AppendLine("    internal void __PerformInitialLayoutTree() { PerformLayout(); foreach (Control child in Controls) child.__PerformInitialLayoutTree(); }");
        output.Append(pad).AppendLine("    internal void __DumpTree(int depth) { var table = this as TableLayoutPanel; var label = this as Label; global::System.Console.Error.WriteLine(\"facade-tree=\" + new string(' ', depth * 2) + GetType().FullName + \"|name=\" + Name + \"|bounds=\" + Bounds.X + \",\" + Bounds.Y + \",\" + Bounds.Width + \",\" + Bounds.Height + \"|dock=\" + Dock + \"|anchor=\" + __anchor + \"|margin=\" + __margin.Left + \",\" + __margin.Top + \",\" + __margin.Right + \",\" + __margin.Bottom + \"|visible=\" + Visible + \"|autosize=\" + __autoSize + \"|autosize-mode=\" + __autoSizeMode + \"|preferred=\" + __preferredSize.Width + \",\" + __preferredSize.Height + \"|raster=\" + __native.SupportsRaster + \"|children=\" + Controls.Count + (label is null ? string.Empty : \"|label-align=\" + label.__ProjectionTextAlign) + (table is null ? string.Empty : table.__TableTrace())); foreach (Control child in Controls) child.__DumpTree(depth + 1); }");
        output.Append(pad).AppendLine("    internal virtual void __RaiseLoad() { if (!__BeginLoad()) return; __RaiseChildrenLoad(); __RenderManagedPaint(); }");
    }
    if (type.FullName == "System.Windows.Forms.Application")
    {
        output.Append(pad).AppendLine("    public static string LastHostTrace { get; private set; } = string.Empty;");
        output.Append(pad).AppendLine("    public static global::System.Exception? LastCallbackException { get; private set; }");
        output.Append(pad).AppendLine("    public static int CallbackFaultCount { get; private set; }");
        output.Append(pad).AppendLine("    public static event global::System.Threading.ThreadExceptionEventHandler? ThreadException;");
        output.Append(pad).AppendLine("    [global::System.ThreadStatic] private static ApplicationContext? __context;");
        output.Append(pad).AppendLine("    [global::System.ThreadStatic] internal static Form? __CurrentForm;");
        output.Append(pad).AppendLine("    [global::System.ThreadStatic] private static ToolStripDropDown? __activeMenu;");
        output.Append(pad).AppendLine("    private static readonly object __contextsGate = new();");
        output.Append(pad).AppendLine("    private static readonly global::System.Collections.Generic.Dictionary<int, ApplicationContext> __contexts = new();");
        output.Append(pad).AppendLine("    internal static void __RegisterMenu(ToolStripDropDown menu) { var root = menu.__MenuRoot; if (global::System.Object.ReferenceEquals(__activeMenu, root)) return; var previous = __activeMenu; __activeMenu = root; previous?.__CloseDropDown(); }");
        output.Append(pad).AppendLine("    internal static void __MenuClosed(ToolStripDropDown menu) { if (global::System.Object.ReferenceEquals(__activeMenu, menu)) __activeMenu = null; }");
        output.Append(pad).AppendLine("    internal static void __DismissActiveMenuForPointer(Control target) { var menu = __activeMenu; if (menu is not null && !menu.__ContainsMenuTarget(target)) menu.__CloseDropDown(); }");
        output.Append(pad).AppendLine("    internal static bool __DispatchKeyToActiveMenu(Control sender, uint physicalKey, bool down, uint modifiers, bool repeat) { var menu = __activeMenu; if (menu is null || menu.__ContainsMenuTarget(sender)) return false; menu.__KeyboardTarget.__DeliverNativeKey(physicalKey, down, modifiers, repeat); return true; }");
        output.Append(pad).AppendLine("    internal static void __CloseActiveMenu() { var menu = __activeMenu; __activeMenu = null; menu?.__CloseDropDown(); }");
        output.Append(pad).AppendLine("    internal static bool __Post(int ownerThreadId, global::System.Action action) { ApplicationContext? context; lock (__contextsGate) __contexts.TryGetValue(ownerThreadId, out context); var form = context?.MainForm; if (form is null) return false; try { _ = form.BeginInvoke(action); return true; } catch (global::System.Exception error) { __ReportCallbackException(error); return false; } }");
        output.Append(pad).AppendLine("    internal static void __ReportCallbackException(global::System.Exception error) { LastCallbackException = error; ++CallbackFaultCount; if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_CALLBACKS\") == \"1\") global::System.Console.Error.WriteLine(\"facade-callback-fault=\" + error.ToString().Replace('\\r', ' ').Replace('\\n', ' ')); try { ThreadException?.Invoke(null, new global::System.Threading.ThreadExceptionEventArgs(error)); } catch { } }");
        output.Append(pad).AppendLine("    private static void __RunContext(ApplicationContext context) { if (context is null) throw new global::System.ArgumentNullException(nameof(context)); if (__context is not null) throw new global::System.InvalidOperationException(\"A GUI.Forms application context is already running on this thread.\"); var form = context.MainForm ?? throw new global::System.InvalidOperationException(\"ApplicationContext.MainForm is required.\"); var ownerThreadId = global::System.Environment.CurrentManagedThreadId; LastHostTrace = string.Empty; LastCallbackException = null; CallbackFaultCount = 0; __context = context; __CurrentForm = form; lock (__contextsGate) __contexts.Add(ownerThreadId, context); try { form.__QueueInitialShow(); LastHostTrace = form.__RunNativeWindow(); } finally { __CloseActiveMenu(); lock (__contextsGate) __contexts.Remove(ownerThreadId); try { context.__NotifyThreadExit(); } catch (global::System.Exception error) { __ReportCallbackException(error); } __CurrentForm = null; __context = null; } }");
        output.Append(pad).AppendLine("    public static void Run(ApplicationContext context) { __RunContext(context); }");
    }
    if (type.FullName == "System.Windows.Forms.Form")
    {
        output.Append(pad).AppendLine("    internal override bool __NativeEvent(NativeEvent kind) { if (kind == NativeEvent.FormClosing) { var cancelled = __RaiseFormClosing(CloseReason.UserClosing); if (!cancelled) __CloseOwnedForms(CloseReason.FormOwnerClosing); else if (__modal) __dialogResult = DialogResult.None; return cancelled; } if (kind == NativeEvent.FormClosed) { Visible = false; __RaiseFormClosed(CloseReason.UserClosing); return false; } return base.__NativeEvent(kind); }");
        output.Append(pad).AppendLine("    internal override void __RaiseLoad() { if (!__BeginLoad()) return; if (__TraceLifecycle) global::System.Console.Error.WriteLine(\"facade-load=begin|type=\" + GetType().FullName); OnLoad(global::System.EventArgs.Empty); if (__TraceLifecycle) global::System.Console.Error.WriteLine(\"facade-load=end|type=\" + GetType().FullName); __RaiseChildrenLoad(); __RenderManagedPaintTree(); if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_TREE\") == \"1\") __DumpTree(0); }");
        output.Append(pad).AppendLine("    internal void __QueueInitialShow() { if (__presentationPhase is FormPresentationPhase.initializing or FormPresentationPhase.ready or FormPresentationPhase.closing) throw new global::System.InvalidOperationException(\"The form is already being presented.\"); __closedRaised = false; __closing = false; __presentationPhase = FormPresentationPhase.initializing; _ = BeginInvoke((global::System.Action)(() => { if (__presentationPhase != FormPresentationPhase.initializing) return; __PerformInitialLayoutTree(); __RaiseLoad(); if (__presentationPhase != FormPresentationPhase.initializing) return; Visible = true; __EnsureInitialFocus(); if (__presentationPhase == FormPresentationPhase.initializing) __presentationPhase = FormPresentationPhase.ready; })); }");
        output.Append(pad).AppendLine("    internal void __ShowNonModal() { if (__hostedNonModal && Visible) { __ProjectHostedRoot(); _ = BeginInvoke((global::System.Action)(() => { if (__hostedNonModal && Visible) __ProjectHostedRoot(); })); return; } if (__presentationPhase is FormPresentationPhase.initializing or FormPresentationPhase.ready or FormPresentationPhase.closing) throw new global::System.InvalidOperationException(\"The form is already being presented.\"); __presentationPhase = FormPresentationPhase.initializing; __closedRaised = false; __closing = false; __previousFocus = Control.__FocusedControl; var host = __ownerForm ?? Application.__CurrentForm; if (host is not null && !global::System.Object.ReferenceEquals(host, this) && Parent is null) { __SetOwner(host); __hostedNonModal = true; __topLevel = false; var bounds = Bounds; var availableWidth = global::System.Math.Max(160, host.Width - 32); var availableHeight = global::System.Math.Max(120, host.Height - 32); bounds.Width = global::System.Math.Clamp(bounds.Width > 0 ? bounds.Width : 360, 160, availableWidth); bounds.Height = global::System.Math.Clamp(bounds.Height > 0 ? bounds.Height : 420, 120, availableHeight); bounds.X = global::System.Math.Clamp(bounds.X, 0, global::System.Math.Max(0, host.Width - bounds.Width)); bounds.Y = global::System.Math.Clamp(bounds.Y, 0, global::System.Math.Max(0, host.Height - bounds.Height)); Bounds = bounds; host.Controls.Add(this); BringToFront(); } __EnsureHostedCaptionProjection(); __PerformInitialLayoutTree(); __RaiseLoad(); if (__presentationPhase != FormPresentationPhase.initializing) return; Visible = true; __ProjectHostedRoot(); __EnsureInitialFocus(); if (__presentationPhase == FormPresentationPhase.initializing) __presentationPhase = FormPresentationPhase.ready; _ = BeginInvoke((global::System.Action)(() => { if (__hostedNonModal && Visible) __ProjectHostedRoot(); })); if (__TraceLifecycle) global::System.Console.Error.WriteLine(\"facade-window=show-nonmodal|type=\" + GetType().FullName + \"|hosted=\" + __hostedNonModal + \"|bounds=\" + Bounds.X + \",\" + Bounds.Y + \",\" + Bounds.Width + \",\" + Bounds.Height); }");
        output.Append(pad).AppendLine("    internal void __CloseNonModalOrRequest() { if (!__hostedNonModal) { __RequestClose(); return; } if (__RaiseFormClosing(CloseReason.UserClosing)) return; __CloseOwnedForms(CloseReason.FormOwnerClosing); var parent = Parent; Visible = false; Control.__ClearFocusWithin(this); parent?.Controls.Remove(this); __hostedNonModal = false; __RaiseFormClosed(CloseReason.UserClosing); Control.__RestoreFocus(__previousFocus); if (__TraceLifecycle) global::System.Console.Error.WriteLine(\"facade-window=close-nonmodal|type=\" + GetType().FullName); }");
        output.Append(pad).AppendLine("    protected virtual void OnLoad(global::System.EventArgs e) { Load?.Invoke(this, e); }");
    }
    if (type.FullName == "System.Windows.Forms.CheckBox")
    {
        output.Append(pad).AppendLine("    internal override bool __NativeEvent(NativeEvent kind) { if (kind == NativeEvent.Clicked) { if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=checkbox-native-begin|name=\" + Name); CheckState = (CheckState)__NativeCheckState; if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=checkbox-native-end|name=\" + Name + \"|checked=\" + Checked); } return base.__NativeEvent(kind); }");
    }
    if (type.FullName == "System.Windows.Forms.Button")
    {
        output.Append(pad).AppendLine("    internal override bool __NativeEvent(NativeEvent kind) { var result = base.__NativeEvent(kind); if (kind == NativeEvent.Clicked && __buttonDialogResult != DialogResult.None && FindForm() is Form form) form.DialogResult = __buttonDialogResult; return result; }");
        output.Append(pad).AppendLine("    protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); if (e.Button == MouseButtons.Left && !__NativePromotesPointerClick) __NativeEvent(NativeEvent.Clicked); }");
    }
    if (type.FullName == "System.Windows.Forms.RadioButton")
    {
        output.Append(pad).AppendLine("    internal override bool __NativeEvent(NativeEvent kind) { if (kind == NativeEvent.Clicked) { if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=radio-native-begin|name=\" + Name); if (Parent is not null) foreach (Control peer in Parent.Controls) if (peer is RadioButton radio && !global::System.Object.ReferenceEquals(radio, this)) radio.Checked = false; if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=radio-native-peers|name=\" + Name); Checked = __NativeCheckState != 0u; if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=radio-native-end|name=\" + Name + \"|checked=\" + Checked); } return base.__NativeEvent(kind); }");
    }
    if (type.FullName == "System.Windows.Forms.UserControl")
    {
        output.Append(pad).AppendLine("    internal override void __RaiseLoad() { if (!__BeginLoad()) return; if (__TraceLifecycle) global::System.Console.Error.WriteLine(\"facade-load=begin|type=\" + GetType().FullName); OnLoad(global::System.EventArgs.Empty); if (__TraceLifecycle) global::System.Console.Error.WriteLine(\"facade-load=end|type=\" + GetType().FullName); __RaiseChildrenLoad(); __RenderManagedPaint(); }");
    }
    if (type.FullName == "System.Windows.Forms.Control+ControlCollection")
    {
        output.Append(pad).AppendLine("    private readonly Control __owner;");
        output.Append(pad).AppendLine("    private readonly global::System.Collections.Generic.List<Control> __items = new();");
        output.Append(pad).AppendLine("    internal ControlCollection(Control owner) { __owner = owner; }");
        output.Append(pad).AppendLine("    public override int Count { get { return __items.Count; } }");
        output.Append(pad).AppendLine("    public override global::System.Collections.IEnumerator GetEnumerator() { return __items.GetEnumerator(); }");
        output.Append(pad).AppendLine("    internal void __BringToFront(Control child) { if (__items.Remove(child)) { __items.Insert(0, child); __owner.__native.SetChildIndex(child.__native, 0); __owner.PerformLayout(); } }");
        output.Append(pad).AppendLine("    internal void __SendToBack(Control child) { if (__items.Remove(child)) { __items.Add(child); __owner.__native.SetChildIndex(child.__native, global::System.Math.Max(0, __items.Count - 1)); __owner.PerformLayout(); } }");
    }

    EmitBehaviorMembers(output, type, pad);

    var members = selected[type];
    var methods = members.OfType<MethodInfo>().ToArray();
    var constructors = members.OfType<ConstructorInfo>().ToArray();
    var fields = members.OfType<FieldInfo>().ToArray();
    var accessorMethods = methods.Where(method => method.IsSpecialName).ToHashSet();
    foreach (var field in fields.Where(field => !type.IsEnum).OrderBy(field => field.MetadataToken))
    {
        output.Append(pad).Append("    ")
            .Append(field.IsPublic ? "public " :
                    field.IsFamily || field.IsFamilyOrAssembly ? "protected " :
                    "internal ");
        if (field.IsLiteral) output.Append("const ");
        else if (field.IsStatic) output.Append("static ");
        if (field.IsInitOnly) output.Append("readonly ");
        output.Append(CsType(field.FieldType)).Append(' ').Append(Escape(field.Name));
        if (field.IsLiteral)
            output.Append(" = ").Append(Convert.ToString(
                field.GetRawConstantValue(),
                System.Globalization.CultureInfo.InvariantCulture)).Append(';');
        else if (field.IsStatic && field.IsInitOnly) output.Append(" = default;");
        else output.Append(';');
        output.AppendLine();
    }
    if (!type.IsInterface && !type.IsValueType && !isStatic &&
        type.FullName != "System.Windows.Forms.LayoutEventArgs" &&
        type.FullName != "System.Drawing.Design.PaintValueEventArgs" &&
        !constructors.Any(constructor => constructor.GetParameters().Length == 0))
        output.Append(pad).Append("    public ").Append(CleanName(type).Split('<')[0]).Append("() ")
            .AppendLine(ConstructorBody(type, null));
    foreach (var constructor in constructors.OrderBy(MemberSignature, StringComparer.Ordinal))
    {
        output.Append(pad).Append("    public ").Append(CleanName(type).Split('<')[0]).Append('(')
            .Append(Parameters(constructor.GetParameters(), true)).Append(") ")
            .AppendLine(InstrumentBody(ConstructorBody(type, constructor), type, constructor));
    }
    foreach (var property in type.GetProperties(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
                 .Where(property => (property.GetMethod is not null && accessorMethods.Contains(property.GetMethod)) ||
                                    (property.SetMethod is not null && accessorMethods.Contains(property.SetMethod)))
                 .OrderBy(property => property.Name, StringComparer.Ordinal))
    {
        var access = property.GetMethod ?? property.SetMethod!;
        output.Append(pad).Append("    ").Append(Access(access)).Append(' ');
        if (access.IsStatic) output.Append("static ");
        else if (access.IsVirtual && !access.IsFinal && !type.IsValueType)
            output.Append(CanEmitOverride(type, access, selected) ? "override " : "virtual ");
        output.Append(CsType(property.PropertyType)).Append(' ');
        var index = property.GetIndexParameters();
        if (index.Length == 0) output.Append(Escape(property.Name));
        else output.Append("this[").Append(Parameters(index, false)).Append(']');
        output.Append(' ').AppendLine(InstrumentPropertyBody(
            PropertyBody(type, property, accessorMethods), type, property, accessorMethods));
    }
    foreach (var evt in type.GetEvents(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static | BindingFlags.DeclaredOnly)
                 .Where(evt => (evt.AddMethod is not null && accessorMethods.Contains(evt.AddMethod)) ||
                               (evt.RemoveMethod is not null && accessorMethods.Contains(evt.RemoveMethod)))
                 .OrderBy(evt => evt.Name, StringComparer.Ordinal))
    {
        var access = evt.AddMethod ?? evt.RemoveMethod!;
        output.Append(pad).Append("    ").Append(Access(access)).Append(' ');
        if (access.IsStatic) output.Append("static ");
        else if (access.IsVirtual && !access.IsFinal)
            output.Append(CanEmitOverride(type, access, selected) ? "override " : "virtual ");
        output.Append("event ").Append(CsType(evt.EventHandlerType!)).Append(' ').Append(Escape(evt.Name)).AppendLine(";");
    }
    if (methods.Any(method => method.Name == "op_Inequality"))
    {
        var self = CsType(type);
        output.Append(pad).Append("    public static bool operator !=(").Append(self).Append(" left, ").Append(self).AppendLine(" right) { return !global::System.Object.ReferenceEquals(left, right); }");
        output.Append(pad).Append("    public static bool operator ==(").Append(self).Append(" left, ").Append(self).AppendLine(" right) { return global::System.Object.ReferenceEquals(left, right); }");
        output.Append(pad).AppendLine("    public override bool Equals(object? value) { return global::System.Object.ReferenceEquals(this, value); }");
        output.Append(pad).AppendLine("    public override int GetHashCode() { return global::System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this); }");
    }
    foreach (var method in methods.Where(method => !method.IsSpecialName).OrderBy(MemberSignature, StringComparer.Ordinal))
    {
        output.Append(pad).Append("    ").Append(Access(method)).Append(' ');
        if (method.IsStatic) output.Append("static ");
        else if (type.FullName == "System.Windows.Forms.Control" && method.Name == "Dispose" && method.GetParameters().Length == 1) output.Append("override ");
        else if (!type.IsInterface && method.IsVirtual && !method.IsFinal && !type.IsValueType)
            output.Append(CanEmitOverride(type, method, selected) ? "override " : "virtual ");
        output.Append(CsType(method.ReturnType)).Append(' ').Append(Escape(method.Name));
        if (method.IsGenericMethodDefinition)
            output.Append('<').Append(string.Join(",", method.GetGenericArguments().Select(arg => arg.Name))).Append('>');
        output.Append('(').Append(Parameters(method.GetParameters(), true)).Append(") ");
        output.AppendLine(type.IsInterface ? ";" : InstrumentBody(MethodBody(type, method), type, method));
    }
    foreach (var nested in owned.Where(candidate => candidate.DeclaringType == type).OrderBy(candidate => candidate.Name, StringComparer.Ordinal))
        EmitType(output, nested, owned, selected, indent + 1);
    output.Append(pad).AppendLine("}");
}

static void EmitBehaviorMembers(StringBuilder output, Type type, string pad)
{
    void Add(string value) => output.Append(pad).Append("    ").AppendLine(value);
    switch (type.FullName)
    {
        case "System.Drawing.Design.PaintValueEventArgs":
            Add("private global::System.ComponentModel.ITypeDescriptorContext? __paintValueContext;");
            Add("private object? __paintValue;");
            Add("private global::System.Drawing.Graphics? __paintValueGraphics;");
            Add("private global::System.Drawing.Rectangle __paintValueBounds;");
            break;
        case "System.Drawing.Image":
            Add("internal NativeDrawingBridge.Handle __bitmap;");
            Add("internal int __width;");
            Add("internal int __height;");
            Add("internal global::System.Drawing.Imaging.PixelFormat __pixelFormat = global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb;");
            Add("internal bool __imageDisposed;");
            Add("internal global::System.WeakReference<global::System.Drawing.Graphics>? __graphicsOwner;");
            Add("internal void __AttachGraphics(global::System.Drawing.Graphics graphics) { if (__graphicsOwner is not null && __graphicsOwner.TryGetTarget(out var current) && !current.__graphicsDisposed) throw new global::System.InvalidOperationException(\"Image already has an active Graphics owner.\"); __graphicsOwner = new global::System.WeakReference<global::System.Drawing.Graphics>(graphics); }");
            Add("internal void __DetachGraphics(global::System.Drawing.Graphics graphics) { if (__graphicsOwner is not null && __graphicsOwner.TryGetTarget(out var current) && global::System.Object.ReferenceEquals(current, graphics)) __graphicsOwner = null; }");
            Add("internal void __FlushGraphics() { if (__graphicsOwner is not null && __graphicsOwner.TryGetTarget(out var graphics) && !graphics.__graphicsDisposed) global::System.Drawing.NativeDrawingBridge.Flush(graphics); }");
            Add("internal NativeDrawingBridge.Handle __BitmapHandle { get { return __bitmap; } }");
            break;
        case "System.Drawing.Icon":
            Add("internal byte[] __iconData = global::System.Array.Empty<byte>();");
            Add("internal bool __iconDisposed;");
            break;
        case "System.Drawing.Brush":
            Add("internal NativeDrawingBridge.Handle __handle;");
            Add("internal global::System.Drawing.Color __color;");
            Add("internal bool __brushDisposed;");
            break;
        case "System.Drawing.Pen":
            Add("internal NativeDrawingBridge.Handle __handle;");
            Add("internal NativeDrawingBridge.Handle __brushHandle;");
            Add("internal float __width = 1f;");
            Add("internal NativeDrawingBridge.Handle __BrushHandle => __brushHandle;");
            Add("internal float __StrokeWidth => __width;");
            Add("internal bool __penDisposed;");
            break;
        case "System.Drawing.Font":
            Add("internal NativeDrawingBridge.Handle __handle;");
            Add("internal string __family = \"Lucida Grande\";");
            Add("internal float __size = 12f;");
            Add("internal global::System.Drawing.FontStyle __style;");
            Add("internal global::System.Drawing.GraphicsUnit __unit = global::System.Drawing.GraphicsUnit.Point;");
            Add("internal bool __fontDisposed;");
            break;
        case "System.Drawing.FontFamily":
            Add("internal string __name = \"Lucida Grande\";");
            break;
        case "System.Drawing.StringFormat":
            Add("internal NativeDrawingBridge.Handle __handle;");
            Add("internal global::System.Drawing.StringAlignment __alignment;");
            Add("internal global::System.Drawing.StringAlignment __lineAlignment;");
            Add("internal global::System.Drawing.StringTrimming __trimming;");
            Add("internal global::System.Drawing.StringFormatFlags __flags;");
            Add("internal bool __formatDisposed;");
            break;
        case "System.Drawing.Graphics":
            Add("internal NativeDrawingBridge.Handle __recorder;");
            Add("internal ulong __executedCommands;");
            Add("internal void __EnsureRecorder() { if (__recorder.IsNull) __recorder = NativeDrawingBridge.RecorderCreate(); }");
            Add("internal int __savedStateDepth;");
            Add("internal bool __hasClip;");
            Add("internal global::System.Drawing.RectangleF __clip;");
            Add("internal float __nativeOriginX;");
            Add("internal float __nativeOriginY;");
            Add("internal global::System.Drawing.Image? __target;");
            Add("internal nint __nativeSurface;");
            Add("internal uint __nativeSurfaceKind;");
            Add("internal nint __leasedHdc;");
            Add("internal ulong __hdcLeaseToken;");
            Add("internal bool __graphicsDisposed;");
            Add("internal void __DrawLine(global::System.Drawing.Pen pen, global::System.Drawing.PointF from, global::System.Drawing.PointF to) { __EnsureRecorder(); if (!pen.__BrushHandle.IsNull && (from.X == to.X || from.Y == to.Y)) { var half = pen.__StrokeWidth * 0.5f; var left = global::System.Math.Min(from.X, to.X); var top = global::System.Math.Min(from.Y, to.Y); var width = from.X == to.X ? pen.__StrokeWidth : global::System.Math.Abs(to.X - from.X) + 1f; var height = from.Y == to.Y ? pen.__StrokeWidth : global::System.Math.Abs(to.Y - from.Y) + 1f; NativeDrawingBridge.FillRectangle(__recorder, pen.__BrushHandle, new global::System.Drawing.RectangleF(left - (from.X == to.X ? half : 0f), top - (from.Y == to.Y ? half : 0f), width, height)); return; } NativeDrawingBridge.DrawLine(__recorder, pen.__handle, from, to); }");
            Add("internal global::System.Drawing.Drawing2D.SmoothingMode __smoothing;");
            Add("internal global::System.Drawing.Drawing2D.InterpolationMode __interpolation;");
            Add("internal global::System.Drawing.Drawing2D.PixelOffsetMode __pixelOffset;");
            Add("internal global::System.Drawing.Drawing2D.CompositingMode __compositing;");
            Add("internal global::System.Drawing.Drawing2D.CompositingQuality __compositingQuality;");
            Add("internal global::System.Drawing.Drawing2D.Matrix __transform = new global::System.Drawing.Drawing2D.Matrix();");
            break;
        case "System.Drawing.Drawing2D.GraphicsState":
            Add("internal ulong __token;");
            Add("internal global::System.Drawing.Graphics? __owner;");
            Add("internal int __depth;");
            Add("internal bool __restored;");
            Add("internal bool __hasClip;");
            Add("internal global::System.Drawing.RectangleF __clip;");
            Add("internal global::System.Drawing.Drawing2D.Matrix __transform = new global::System.Drawing.Drawing2D.Matrix();");
            Add("internal global::System.Drawing.Drawing2D.SmoothingMode __smoothing;");
            Add("internal global::System.Drawing.Drawing2D.InterpolationMode __interpolation;");
            Add("internal global::System.Drawing.Drawing2D.PixelOffsetMode __pixelOffset;");
            Add("internal global::System.Drawing.Drawing2D.CompositingMode __compositing;");
            Add("internal global::System.Drawing.Drawing2D.CompositingQuality __compositingQuality;");
            break;
        case "System.Drawing.Drawing2D.GraphicsPath":
            Add("internal global::System.Drawing.NativeDrawingBridge.Handle __handle;");
            Add("internal bool __pathDisposed;");
            break;
        case "System.Drawing.Drawing2D.Matrix":
            Add("internal float __m11 = 1f, __m12, __m21, __m22 = 1f, __dx, __dy;");
            break;
        case "System.Drawing.Imaging.ImageAttributes":
            Add("internal global::System.Drawing.NativeDrawingBridge.Handle __handle;");
            Add("internal bool __attributesDisposed;");
            break;
        case "System.Drawing.Region":
            Add("internal NativeDrawingBridge.Handle __handle;");
            Add("internal bool __regionDisposed;");
            break;
        case "System.Drawing.Drawing2D.ColorBlend":
            Add("internal global::System.Drawing.Color[] __colors = global::System.Array.Empty<global::System.Drawing.Color>();");
            Add("internal float[] __positions = global::System.Array.Empty<float>();");
            break;
        case "System.Drawing.Imaging.ColorMap":
            Add("internal global::System.Drawing.Color __oldColor;");
            Add("internal global::System.Drawing.Color __newColor;");
            break;
        case "System.Drawing.Imaging.ColorMatrix":
            Add("internal float[] __values = new float[] { 1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,1,0, 0,0,0,0,1 };");
            break;
        case "System.Drawing.Imaging.BitmapData":
            Add("internal global::System.Drawing.NativeDrawingBridge.Handle __bitmap;");
            Add("internal ulong __token;");
            Add("internal nint __scan0;");
            Add("internal int __stride;");
            Add("internal byte[]? __staging;");
            Add("internal global::System.Runtime.InteropServices.GCHandle __stagingPin;");
            Add("internal nint __nativeScan0;");
            Add("internal int __nativeStride, __lockX, __lockY, __lockWidth, __lockHeight;");
            Add("internal bool __writeBackStraightAlpha;");
            break;
        case "System.Drawing.Imaging.ImageFormat":
            Add("internal static readonly global::System.Drawing.Imaging.ImageFormat __png = new global::System.Drawing.Imaging.ImageFormat();");
            break;
        case "System.Windows.Forms.BaseCollection":
            Add("protected virtual global::System.Collections.IList __Collection { get { return global::System.Array.Empty<object>(); } }");
            break;
        case "System.Windows.Forms.Clipboard":
            Add("private static string __clipboardText = string.Empty;");
            break;
        case "System.Windows.Forms.CommonDialog":
            Add("internal virtual DialogResult __RunDialog(IWin32Window? owner) { if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_DIALOGS\") == \"1\") global::System.Console.Error.WriteLine(\"facade-dialog=cancel|type=\" + GetType().FullName + \"|reason=no-host-provider\"); return DialogResult.Cancel; }");
            break;
        case "System.Windows.Forms.FileDialog":
            Add("private string __defaultExt = string.Empty;");
            Add("protected string __fileName = string.Empty;");
            Add("private string __filter = string.Empty;");
            Add("private string __initialDirectory = string.Empty;");
            Add("private bool __restoreDirectory;");
            Add("internal override DialogResult __RunDialog(IWin32Window? owner) { var selected = global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_DIALOG_FILE\"); if (!global::System.String.IsNullOrWhiteSpace(selected)) { __fileName = selected; if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_DIALOGS\") == \"1\") global::System.Console.Error.WriteLine(\"facade-dialog=file|result=OK|path=\" + selected); return DialogResult.OK; } var host = owner as Control ?? Application.__CurrentForm; if (host is null) return base.__RunDialog(owner); var kind = this is SaveFileDialog ? 2u : 1u; var flags = this is SaveFileDialog ? 2u : 0u; if (host.__ShowPathDialog(kind, this is SaveFileDialog ? \"Save file\" : \"Open file\", __initialDirectory, __fileName, __defaultExt, __filter, flags, out selected)) { __fileName = selected; if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_DIALOGS\") == \"1\") global::System.Console.Error.WriteLine(\"facade-dialog=file|result=OK|path=\" + selected + \"|provider=native\"); return DialogResult.OK; } if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_DIALOGS\") == \"1\") global::System.Console.Error.WriteLine(\"facade-dialog=file|result=Cancel|provider=native\"); return DialogResult.Cancel; }");
            break;
        case "System.Windows.Forms.FolderBrowserDialog":
            Add("private string __selectedPath = string.Empty;");
            Add("internal override DialogResult __RunDialog(IWin32Window? owner) { var selected = global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_DIALOG_FOLDER\"); if (!global::System.String.IsNullOrWhiteSpace(selected)) { __selectedPath = selected; if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_DIALOGS\") == \"1\") global::System.Console.Error.WriteLine(\"facade-dialog=folder|result=OK|path=\" + selected); return DialogResult.OK; } var host = owner as Control ?? Application.__CurrentForm; if (host is null) return base.__RunDialog(owner); if (host.__ShowPathDialog(3u, \"Select folder\", __selectedPath, string.Empty, string.Empty, string.Empty, 0u, out selected)) { __selectedPath = selected; if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_DIALOGS\") == \"1\") global::System.Console.Error.WriteLine(\"facade-dialog=folder|result=OK|path=\" + selected + \"|provider=native\"); return DialogResult.OK; } if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_DIALOGS\") == \"1\") global::System.Console.Error.WriteLine(\"facade-dialog=folder|result=Cancel|provider=native\"); return DialogResult.Cancel; }");
            break;
        case "System.Windows.Forms.ColorDialog":
            Add("private bool __anyColor;");
            Add("private global::System.Drawing.Color __dialogColor = global::System.Drawing.Color.Black;");
            Add("private bool __fullOpen;");
            break;
        case "System.Windows.Forms.ToolTip":
            Add("private readonly global::System.Collections.Generic.Dictionary<Control, string> __toolTips = new();");
            Add("private bool __toolTipActive = true;");
            Add("private int __autoPopDelay = 5000;");
            Add("private int __initialDelay = 500;");
            Add("private int __reshowDelay = 100;");
            Add("private bool __showAlways;");
            Add("private IWin32Window? __shownWindow;");
            Add("private string __shownText = string.Empty;");
            Add("private readonly global::System.Collections.Generic.Dictionary<Control, (global::System.EventHandler Enter, global::System.EventHandler Leave)> __toolTipHandlers = new();");
            Add("private global::System.Threading.Timer? __toolTipTimer;");
            Add("private long __toolTipGeneration;");
            Add("private void __CancelPending(Control? control, bool hide) { global::System.Threading.Interlocked.Increment(ref __toolTipGeneration); var timer = global::System.Threading.Interlocked.Exchange(ref __toolTipTimer, null); timer?.Dispose(); if (hide && control is not null) { try { control.__HideToolTip(); } catch (global::System.InvalidOperationException) { } } }");
            Add("private void __Schedule(Control control, string text) { __CancelPending(control, true); if (!__toolTipActive || text.Length == 0 || (!__showAlways && !control.Enabled)) return; var generation = global::System.Threading.Interlocked.Read(ref __toolTipGeneration); __toolTipTimer = new global::System.Threading.Timer(_ => { try { _ = control.BeginInvoke((global::System.Action)(() => { if (generation != global::System.Threading.Interlocked.Read(ref __toolTipGeneration) || !__toolTipActive || control.IsDisposed) return; control.__ShowToolTip(text, 12, control.Height + 4, __autoPopDelay); __shownWindow = control; __shownText = text; })); } catch (global::System.InvalidOperationException) { } }, null, __initialDelay, global::System.Threading.Timeout.Infinite); }");
            break;
        case "System.Windows.Forms.MessageBox":
            Add("private static DialogResult __Show(string text, string caption, MessageBoxButtons buttons, MessageBoxIcon icon, MessageBoxDefaultButton defaultButton) { using var dialog = new Form { Text = caption ?? string.Empty, ClientSize = new global::System.Drawing.Size(420, 150), FormBorderStyle = FormBorderStyle.FixedDialog, MaximizeBox = false, MinimizeBox = false, ShowInTaskbar = false, StartPosition = FormStartPosition.CenterScreen }; var message = new Label { Text = text ?? string.Empty, Bounds = new global::System.Drawing.Rectangle(18, 18, 384, 72) }; dialog.Controls.Add(message); var choices = buttons switch { MessageBoxButtons.OKCancel => new[] { (\"OK\", DialogResult.OK), (\"Cancel\", DialogResult.Cancel) }, MessageBoxButtons.YesNo => new[] { (\"Yes\", DialogResult.Yes), (\"No\", DialogResult.No) }, MessageBoxButtons.YesNoCancel => new[] { (\"Yes\", DialogResult.Yes), (\"No\", DialogResult.No), (\"Cancel\", DialogResult.Cancel) }, MessageBoxButtons.RetryCancel => new[] { (\"Retry\", DialogResult.Retry), (\"Cancel\", DialogResult.Cancel) }, MessageBoxButtons.AbortRetryIgnore => new[] { (\"Abort\", DialogResult.Abort), (\"Retry\", DialogResult.Retry), (\"Ignore\", DialogResult.Ignore) }, MessageBoxButtons.CancelTryContinue => new[] { (\"Cancel\", DialogResult.Cancel), (\"Try Again\", DialogResult.TryAgain), (\"Continue\", DialogResult.Continue) }, _ => new[] { (\"OK\", DialogResult.OK) } }; var totalWidth = choices.Length * 88; var left = global::System.Math.Max(12, 420 - totalWidth - 12); for (var index = 0; index < choices.Length; ++index) { var choice = choices[index]; var button = new Button { Text = choice.Item1, Bounds = new global::System.Drawing.Rectangle(left + index * 88, 106, 80, 26), DialogResult = choice.Item2 }; dialog.Controls.Add(button); } var result = dialog.ShowDialog(); return result == DialogResult.None ? DialogResult.Cancel : result; }");
            break;
        case "System.Windows.Forms.DrawItemEventArgs":
            Add("internal global::System.Drawing.Rectangle __drawBounds;");
            Add("internal global::System.Drawing.Graphics? __drawGraphics;");
            Add("internal int __drawIndex = -1;");
            Add("internal DrawItemState __drawState;");
            break;
        case "System.Windows.Forms.ConvertEventArgs":
            Add("private object? __convertedValue;");
            break;
        case "System.Windows.Forms.CreateParams":
            Add("private int __extendedStyle;");
            break;
        case "System.Windows.Forms.Cursor":
            Add("private static Cursor? __current;");
            Add("private static global::System.Drawing.Point __position;");
            Add("private readonly uint __kind;");
            Add("internal Cursor(uint kind) { __kind = kind; }");
            Add("internal uint __Kind { get { return __kind; } }");
            break;
        case "System.Windows.Forms.DateTimePicker":
            Add("private global::System.Drawing.Color __calendarForeColor;");
            Add("private global::System.Drawing.Color __calendarMonthBackground;");
            Add("private string __customFormat = string.Empty;");
            Add("private DateTimePickerFormat __dateTimeFormat;");
            Add("private global::System.DateTime __minimumDate = global::System.DateTime.MinValue;");
            Add("private global::System.DateTime __maximumDate = global::System.DateTime.MaxValue;");
            Add("private global::System.DateTime __dateTimeValue = global::System.DateTime.Now;");
            break;
        case "System.Windows.Forms.Screen":
            Add("private global::System.Drawing.Rectangle __screenBounds = new global::System.Drawing.Rectangle(0, 0, 1920, 1080);");
            break;
        case "System.Windows.Forms.FormClosingEventArgs":
            Add("private CloseReason __closeReason = CloseReason.UserClosing;");
            Add("internal void __SetCloseReason(CloseReason reason) { __closeReason = reason; }");
            break;
        case "System.Windows.Forms.FormClosedEventArgs":
            Add("private CloseReason __closeReason = CloseReason.UserClosing;");
            Add("internal void __SetCloseReason(CloseReason reason) { __closeReason = reason; }");
            Add("public CloseReason CloseReason { get { return __closeReason; } }");
            break;
        case "System.Windows.Forms.PreviewKeyDownEventArgs":
            Add("private Keys __previewKeyCode;");
            break;
        case "System.Windows.Forms.ToolStripItemRenderEventArgs":
            Add("internal global::System.Drawing.Graphics? __renderGraphics;");
            Add("internal ToolStripItem? __renderItem;");
            Add("internal ToolStrip? __renderToolStrip;");
            break;
        case "System.Windows.Forms.ToolStripItemImageRenderEventArgs":
            Add("internal global::System.Drawing.Image? __renderImage;");
            Add("internal global::System.Drawing.Rectangle __renderImageRectangle;");
            break;
        case "System.Windows.Forms.ToolStripItemTextRenderEventArgs":
            Add("internal string __renderText = string.Empty;");
            Add("internal global::System.Drawing.Font? __renderTextFont;");
            Add("internal TextFormatFlags __renderTextFormat;");
            Add("internal global::System.Drawing.Rectangle __renderTextRectangle;");
            break;
        case "System.Windows.Forms.ToolStripRenderEventArgs":
            Add("internal global::System.Drawing.Graphics? __stripGraphics;");
            Add("internal ToolStrip? __strip;");
            break;
        case "System.Windows.Forms.ToolStripArrowRenderEventArgs":
            Add("private global::System.Drawing.Color __arrowColor;");
            Add("internal ToolStripItem? __arrowItem;");
            break;
        case "System.Windows.Forms.ToolStripGripRenderEventArgs":
            Add("internal global::System.Drawing.Rectangle __gripBounds;");
            break;
        case "System.Windows.Forms.ToolStripSeparatorRenderEventArgs":
            Add("internal bool __verticalSeparator;");
            break;
        case "System.Windows.Forms.Control":
            Add("private static Control? __focusedControl;");
            Add("[global::System.ThreadStatic] private static global::System.Drawing.Point __mousePosition;");
            Add("private static bool __checkForIllegalCrossThreadCalls;");
            Add("private sealed class __DeferredManagedInput { internal readonly Control Target; internal readonly global::System.Action Deliver; internal __DeferredManagedInput(Control target, global::System.Action deliver) { Target = target; Deliver = deliver; } }");
            Add("[global::System.ThreadStatic] private static int __managedPaintLeaseDepth;");
            Add("[global::System.ThreadStatic] private static global::System.Collections.Generic.Queue<__DeferredManagedInput>? __managedPaintInputQueue;");
            Add("[global::System.ThreadStatic] private static bool __drainingManagedPaintInput;");
            Add("[global::System.ThreadStatic] private static bool __managedPaintInputDrainQueued;");
            Add("private bool __PostCrossThreadMutation(global::System.Action mutation) { if (!InvokeRequired) return false; if (__checkForIllegalCrossThreadCalls) throw new global::System.InvalidOperationException(\"Cross-thread operation not valid: Control accessed from a thread other than the thread it was created on.\"); _ = BeginInvoke(mutation); return true; }");
            Add("private Control? __parent;");
            Add("private int __tabIndex;");
            Add("private AnchorStyles __anchor = AnchorStyles.Top | AnchorStyles.Left;");
            Add("private DockStyle __dock;");
            Add("private Padding __margin = new Padding(3);");
            Add("private Padding __padding;");
            Add("private bool __autoSize;");
            Add("private AutoSizeMode __autoSizeMode = AutoSizeMode.GrowOnly;");
            Add("private bool __tabStop = true;");
            Add("private bool __allowDrop;");
            Add("private bool __doubleBuffered;");
            Add("private int __layoutSuspendDepth;");
            Add("private bool __performingLayout;");
            Add("private bool __layoutDeferred;");
            Add("private LayoutEventArgs? __pendingLayoutArgs;");
            Add("private const int __maximumManagedLayoutPasses = 8;");
            Add("private void __QueueLayout(LayoutEventArgs args) { __layoutDeferred = true; __pendingLayoutArgs = args; if (__layoutSuspendDepth == 0 && !__performingLayout) __DrainLayout(); }");
            Add("private void __DrainLayout() { if (__layoutSuspendDepth != 0 || __performingLayout || !__layoutDeferred) return; __performingLayout = true; var pass = 0; try { while (__layoutDeferred && pass++ < __maximumManagedLayoutPasses) { var args = __pendingLayoutArgs ?? new LayoutEventArgs(this, null); __layoutDeferred = false; __pendingLayoutArgs = null; __native.SuspendLayout(); try { OnLayout(args); } catch { __layoutDeferred = true; __pendingLayoutArgs = args; throw; } finally { __native.ResumeLayout(false); } } if (__layoutDeferred && global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_LAYOUT\") == \"1\") global::System.Console.Error.WriteLine(\"facade-layout=pass-limit|type:\" + GetType().FullName + \"|name:\" + Name + \"|passes:\" + pass); } finally { __performingLayout = false; } }");
            Add("private long __styles = (long)ControlStyles.StandardClick;");
            Add("private object? __tag;");
            Add("private global::System.Drawing.Color __backColor;");
            Add("private global::System.Drawing.Color __foreColor;");
            Add("private global::System.Drawing.Font? __font;");
            Add("private global::System.Drawing.Image? __backgroundImage;");
            Add("private ImageLayout __backgroundImageLayout;");
            Add("private Cursor? __cursor;");
            Add("private global::System.Drawing.Region? __region;");
            Add("private ContextMenuStrip? __contextMenuStrip;");
            Add("private RightToLeft __rightToLeft;");
            Add("private global::System.Drawing.Size __minimumSize;");
            Add("private global::System.Drawing.Size __maximumSize;");
            Add("private global::System.Drawing.Size __preferredSize;");
            Add("private global::System.Drawing.Size __lastAutoSizePreferred;");
            Add("private bool __hasAutoSizePreferred;");
            Add("private global::System.Drawing.Point __autoScrollOffset;");
            Add("protected bool __assigningLayoutBounds;");
            Add("internal static uint __CompactContentAlignment(global::System.Drawing.ContentAlignment value) { return value switch { global::System.Drawing.ContentAlignment.TopLeft => 0u, global::System.Drawing.ContentAlignment.TopCenter => 1u, global::System.Drawing.ContentAlignment.TopRight => 2u, global::System.Drawing.ContentAlignment.MiddleLeft => 3u, global::System.Drawing.ContentAlignment.MiddleCenter => 4u, global::System.Drawing.ContentAlignment.MiddleRight => 5u, global::System.Drawing.ContentAlignment.BottomLeft => 6u, global::System.Drawing.ContentAlignment.BottomCenter => 7u, global::System.Drawing.ContentAlignment.BottomRight => 8u, _ => throw new global::System.ComponentModel.InvalidEnumArgumentException(nameof(value), (int)value, typeof(global::System.Drawing.ContentAlignment)) }; }");
            Add("internal bool __InheritsColors { get { return __foreColor.IsEmpty || __backColor.IsEmpty; } }");
            Add("internal global::System.Drawing.Image? __ProjectionBackgroundImage { get { return __backgroundImage; } }");
            Add("internal ImageLayout __ProjectionBackgroundImageLayout { get { return __backgroundImageLayout; } }");
            Add("internal void __SetInitialSize(global::System.Drawing.Size value) { var size = new global::System.Drawing.Size(global::System.Math.Max(0, value.Width), global::System.Math.Max(0, value.Height)); __preferredSize = size; var bounds = __native.Bounds; __native.Bounds = new global::System.Drawing.Rectangle(bounds.Location, size); }");
            Add("internal void __SetLayoutBounds(global::System.Drawing.Rectangle value) { __assigningLayoutBounds = true; try { Bounds = value; } finally { __assigningLayoutBounds = false; } }");
            Add("private void __ApplyEffectiveColors(bool inheritedChange = false) { __native.SetColors(ForeColor, BackColor); if (inheritedChange && __loadRaised && __native.SupportsRaster) __QueueManagedPaint(); foreach (Control child in Controls) if (child.__InheritsColors) child.__ApplyEffectiveColors(true); }");
            Add("internal void __SetNativeFieldSelection(int start, int length, bool caretVisible = true) { __native.SetFieldEditState(start, start + length, caretVisible); }");
            Add("internal void __SetNativeFieldEditState(int anchor, int caret, bool caretVisible = true) { __native.SetFieldEditState(anchor, caret, caretVisible); }");
            Add("internal int __NativeFieldPositionAt(double x) { return __native.FieldPositionFromPoint(x); }");
            Add("internal int __PreviousNativeFieldPosition(int position) { return __native.NavigateFieldPosition(position, -1); }");
            Add("internal int __NextNativeFieldPosition(int position) { return __native.NavigateFieldPosition(position, 1); }");
            Add("internal NativeFieldEdit __ReplaceNativeField(int start, int length, string replacement) { return __native.ReplaceField(start, length, replacement); }");
            Add("internal NativeFieldEdit __NativeFieldHistory(int direction) { return __native.FieldHistory(direction); }");
            Add("internal void __ClearNativeFieldHistory() { __native.ClearFieldHistory(); }");
            Add("internal void __NotifyNativeFieldTextChanged() { __NativeChanged(NativeChange.Text); }");
            Add("internal string __ReadClipboardText() { return __native.ReadClipboardText(); }");
            Add("internal void __WriteClipboardText(string text) { __native.WriteClipboardText(text); }");
            Add("private bool __renderingManagedPaintValue;");
            Add("private bool __renderingManagedPaint { get { return __renderingManagedPaintValue; } set { if (__renderingManagedPaintValue == value) return; __renderingManagedPaintValue = value; if (value) { ++__managedPaintLeaseDepth; return; } if (__managedPaintLeaseDepth > 0) --__managedPaintLeaseDepth; if (__managedPaintLeaseDepth == 0) __PostManagedPaintInputDrain(); } }");
            Add("private global::System.Drawing.Bitmap? __managedPaintSurface;");
            Add("private global::System.Drawing.Size __managedPaintSurfaceSize;");
            Add("private bool __managedPaintSurfaceRetirePending;");
            Add("private sealed class __PaintQueueState { internal int Queued; internal int DirtyAfterRender; internal long ContentRevision = 1; internal long RenderedRevision; internal long SurfaceEpoch = 1; internal long QueueRequests; internal long CoalescedRequests; internal long LeasesStarted; internal long LeasesCompleted; internal long LeasesAbandoned; internal long SurfaceAllocations; internal long SurfaceReuses; internal long SurfaceRetirements; internal long InputsDeferred; internal long InputsDrained; internal long InputsAbandoned; internal readonly object DamageGate = new(); internal global::System.Drawing.Rectangle PendingDamage; internal bool HasPendingDamage; internal bool FullDamage = true; internal global::System.Drawing.Rectangle LastRenderedDamage; internal long PartialDamageTouches; internal long FullDamageTouches; internal long DamageMerges; }");
            Add("private readonly __PaintQueueState __paintState = new();");
            Add("[global::System.ThreadStaticAttribute] private static global::System.Collections.Generic.HashSet<Control>? __callbackPaintQueue;");
            Add("private static void __QueueCallbackPaint(Control control) { (__callbackPaintQueue ??= new global::System.Collections.Generic.HashSet<Control>()).Add(control); }");
            Add("internal static void __FlushCallbackPaintQueue() { var queue = __callbackPaintQueue; if (queue is null || queue.Count == 0) return; var pending = new Control[queue.Count]; queue.CopyTo(pending); queue.Clear(); foreach (var control in pending) if (!control.__native.IsDisposed && global::System.Threading.Volatile.Read(ref control.__paintState.Queued) != 0) control.__RenderManagedPaintNow(); if (queue.Count == 0) return; var deferred = new Control[queue.Count]; queue.CopyTo(deferred); queue.Clear(); foreach (var control in deferred) if (!control.__native.IsDisposed && global::System.Threading.Volatile.Read(ref control.__paintState.Queued) != 0) control.__PostQueuedManagedPaint(); }");
            Add("internal bool __DeferManagedInput(global::System.Action deliver) { if (__managedPaintLeaseDepth == 0) return false; var queue = __managedPaintInputQueue ??= new global::System.Collections.Generic.Queue<__DeferredManagedInput>(); if (queue.Count >= 1024) throw new global::System.InvalidOperationException(\"Managed paint input queue exceeded its bounded capacity.\"); queue.Enqueue(new __DeferredManagedInput(this, deliver)); global::System.Threading.Interlocked.Increment(ref __paintState.InputsDeferred); return true; }");
            Add("private void __PostManagedPaintInputDrain() { var queue = __managedPaintInputQueue; if (queue is null || queue.Count == 0 || __managedPaintInputDrainQueued) return; Control? dispatcher = null; foreach (var input in queue) if (!input.Target.__native.IsDisposed) { dispatcher = input.Target; break; } if (dispatcher is null) { __DrainManagedPaintInput(); return; } __managedPaintInputDrainQueued = true; try { _ = dispatcher.BeginInvoke((global::System.Action)__DrainManagedPaintInput); } catch (global::System.Exception error) { __managedPaintInputDrainQueued = false; Application.__ReportCallbackException(error); } }");
            Add("private static void __DrainManagedPaintInput() { __managedPaintInputDrainQueued = false; if (__managedPaintLeaseDepth != 0 || __drainingManagedPaintInput) return; var queue = __managedPaintInputQueue; if (queue is null || queue.Count == 0) return; __drainingManagedPaintInput = true; try { while (queue.Count != 0) { var input = queue.Dequeue(); if (input.Target.__native.IsDisposed) { global::System.Threading.Interlocked.Increment(ref input.Target.__paintState.InputsAbandoned); continue; } try { input.Deliver(); global::System.Threading.Interlocked.Increment(ref input.Target.__paintState.InputsDrained); } catch (global::System.Exception error) { Application.__ReportCallbackException(error); } } } finally { __drainingManagedPaintInput = false; } }");
            Add("private static void __AbandonManagedPaintInput(Control target) { var queue = __managedPaintInputQueue; if (queue is null || queue.Count == 0) return; var retained = new global::System.Collections.Generic.Queue<__DeferredManagedInput>(queue.Count); while (queue.Count != 0) { var input = queue.Dequeue(); if (global::System.Object.ReferenceEquals(input.Target, target)) global::System.Threading.Interlocked.Increment(ref target.__paintState.InputsAbandoned); else retained.Enqueue(input); } __managedPaintInputQueue = retained; }");
            Add("private int __PendingManagedInputCount() { var queue = __managedPaintInputQueue; if (queue is null || queue.Count == 0) return 0; var count = 0; foreach (var input in queue) if (global::System.Object.ReferenceEquals(input.Target, this)) ++count; return count; }");
            Add("internal void __InjectManagedPointer(uint kind, double x, double y, double wheelDelta, uint button) { __NativePointer(new NativePointer(kind, x, y, wheelDelta, button)); }");
            Add("internal void __InjectManagedKey(uint physicalKey, bool down, uint modifiers, bool repeat) { __NativeKeyIngress(physicalKey, down, modifiers, repeat); }");
            Add("internal void __InjectManagedText(string text, bool composing, int replacementStart, int replacementLength) { __NativeTextIngress(text, composing, replacementStart, replacementLength); }");
            Add("private static readonly global::System.Collections.Concurrent.ConcurrentDictionary<string, byte> __paintTrace = new();");
            Add("private static readonly global::System.Collections.Concurrent.ConcurrentDictionary<string, int> __paintContentTrace = new();");
            Add("private static int __paintCaptureSequence;");
            Add("internal virtual void __NativeKeyInput(uint physicalKey, bool down, uint modifiers, bool repeat) { if (Application.__DispatchKeyToActiveMenu(this, physicalKey, down, modifiers, repeat)) return; __DeliverNativeKey(physicalKey, down, modifiers, repeat); }");
            Add("private void __NativeKeyIngress(uint physicalKey, bool down, uint modifiers, bool repeat) { if (__DeferManagedInput(() => __NativeKeyInput(physicalKey, down, modifiers, repeat))) return; __NativeKeyInput(physicalKey, down, modifiers, repeat); }");
            Add("internal static Keys __KeysFromNative(uint physicalKey, uint modifiers) { var key = physicalKey switch { >= 0x04u and <= 0x1du => (Keys)((int)Keys.A + (int)(physicalKey - 0x04u)), >= 0x1eu and <= 0x26u => (Keys)((int)Keys.D1 + (int)(physicalKey - 0x1eu)), 0x27u => Keys.D0, 0x28u => Keys.Enter, 0x29u => Keys.Escape, 0x2au => Keys.Back, 0x2bu => Keys.Tab, 0x2cu => Keys.Space, 0x4au => Keys.Home, 0x4bu => Keys.PageUp, 0x4cu => Keys.Delete, 0x4du => Keys.End, 0x4eu => Keys.PageDown, 0x4fu => Keys.Right, 0x50u => Keys.Left, 0x51u => Keys.Down, 0x52u => Keys.Up, _ => Keys.None }; if ((modifiers & 1u) != 0) key |= Keys.Shift; if ((modifiers & 2u) != 0) key |= Keys.Control; if ((modifiers & 4u) != 0) key |= Keys.Alt; return key; }");
            Add("internal void __DeliverNativeKey(uint physicalKey, bool down, uint modifiers, bool repeat) { var e = new KeyEventArgs(__KeysFromNative(physicalKey, modifiers)); if (down) OnKeyDown(e); else OnKeyUp(e); }");
            Add("internal virtual void __NativeTextInput(string text, bool composing, int replacementStart, int replacementLength) { }");
            Add("private void __NativeTextIngress(string text, bool composing, int replacementStart, int replacementLength) { if (__DeferManagedInput(() => __NativeTextInput(text, composing, replacementStart, replacementLength))) return; __NativeTextInput(text, composing, replacementStart, replacementLength); }");
            Add("internal void __RenderManagedPaint() { if (!global::System.OperatingSystem.IsWindows() || __native.IsDisposed) return; __TouchManagedPaint(); __RenderManagedPaintWindows(); }");
            Add("internal void __RenderManagedPaintTree() { if (__native.IsDisposed || !Visible) return; __RenderManagedPaint(); foreach (Control child in Controls) child.__RenderManagedPaintTree(); }");
            Add("internal void __QueueManagedPaintTree() { if (__native.IsDisposed || !Visible) return; if (__native.SupportsRaster) __QueueManagedPaint(); foreach (Control child in Controls) child.__QueueManagedPaintTree(); }");
            Add("internal void __RenderWindowSurfaceTree() { if (__native.IsDisposed || !Visible) return; if (__native.HasWindowHandle) __RenderManagedPaint(); foreach (Control child in Controls) child.__RenderWindowSurfaceTree(); }");
            Add("private global::System.Drawing.Color __ManagedPaintBackground() { return BackColor.A == 0 && (__styles & (long)ControlStyles.SupportsTransparentBackColor) != 0 ? global::System.Drawing.Color.Transparent : BackColor; }");
            Add("private bool __DoubleBufferedRequested { get { return __doubleBuffered || (__styles & (long)ControlStyles.OptimizedDoubleBuffer) != 0; } }");
            Add("private void __SetDoubleBuffered(bool value) { if (__DoubleBufferedRequested == value && __doubleBuffered == value) return; __doubleBuffered = value; if (value) __styles |= (long)(ControlStyles.OptimizedDoubleBuffer | ControlStyles.AllPaintingInWmPaint); else __styles &= ~(long)ControlStyles.OptimizedDoubleBuffer; if (!value) __InvalidateManagedPaintSurface(); if (__loadRaised) __QueueManagedPaint(); }");
            Add("private global::System.Drawing.Rectangle __ClipManagedDamage(global::System.Drawing.Rectangle damage) { var left = global::System.Math.Max(0, damage.Left); var top = global::System.Math.Max(0, damage.Top); var right = global::System.Math.Min(Width, damage.Right); var bottom = global::System.Math.Min(Height, damage.Bottom); return right <= left || bottom <= top ? global::System.Drawing.Rectangle.Empty : global::System.Drawing.Rectangle.FromLTRB(left, top, right, bottom); }");
            Add("private bool __RecordManagedDamage(global::System.Drawing.Rectangle? damage) { var bounds = new global::System.Drawing.Rectangle(0, 0, Width, Height); if (bounds.Width <= 0 || bounds.Height <= 0) return false; var clipped = damage.HasValue ? __ClipManagedDamage(damage.Value) : bounds; if (clipped.Width <= 0 || clipped.Height <= 0) return false; lock (__paintState.DamageGate) { if (!damage.HasValue || clipped == bounds) { __paintState.FullDamage = true; __paintState.PendingDamage = bounds; __paintState.HasPendingDamage = true; ++__paintState.FullDamageTouches; return true; } ++__paintState.PartialDamageTouches; if (__paintState.FullDamage) return true; if (!__paintState.HasPendingDamage) { __paintState.PendingDamage = clipped; __paintState.HasPendingDamage = true; return true; } var merged = global::System.Drawing.Rectangle.Union(__paintState.PendingDamage, clipped); if (merged != __paintState.PendingDamage) ++__paintState.DamageMerges; __paintState.PendingDamage = merged; return true; } }");
            Add("private void __TouchManagedPaint() { _ = __TouchManagedPaint(null); }");
            Add("private bool __TouchManagedPaint(global::System.Drawing.Rectangle? damage) { if (!__RecordManagedDamage(damage)) return false; global::System.Threading.Interlocked.Increment(ref __paintState.ContentRevision); if (__renderingManagedPaint) global::System.Threading.Interlocked.Exchange(ref __paintState.DirtyAfterRender, 1); return true; }");
            Add("private global::System.Drawing.Rectangle __TakeManagedDamage(int width, int height, bool persistent) { var bounds = new global::System.Drawing.Rectangle(0, 0, width, height); lock (__paintState.DamageGate) { var damage = !persistent || __paintState.FullDamage || !__paintState.HasPendingDamage ? bounds : __paintState.PendingDamage; __paintState.PendingDamage = global::System.Drawing.Rectangle.Empty; __paintState.HasPendingDamage = false; __paintState.FullDamage = false; return damage; } }");
            Add("private void __RestoreManagedDamage(global::System.Drawing.Rectangle damage, bool full) { lock (__paintState.DamageGate) { if (full) { __paintState.FullDamage = true; __paintState.PendingDamage = new global::System.Drawing.Rectangle(0, 0, Width, Height); __paintState.HasPendingDamage = Width > 0 && Height > 0; return; } if (!__paintState.HasPendingDamage) { __paintState.PendingDamage = damage; __paintState.HasPendingDamage = true; return; } __paintState.PendingDamage = global::System.Drawing.Rectangle.Union(__paintState.PendingDamage, damage); } }");
            Add("private string __ManagedDamageSnapshot() { lock (__paintState.DamageGate) { var pending = __paintState.HasPendingDamage ? __paintState.PendingDamage : global::System.Drawing.Rectangle.Empty; var last = __paintState.LastRenderedDamage; return \"damage:\" + pending.X + \",\" + pending.Y + \",\" + pending.Width + \",\" + pending.Height + \"|damage-full:\" + (__paintState.FullDamage ? 1 : 0) + \"|last-damage:\" + last.X + \",\" + last.Y + \",\" + last.Width + \",\" + last.Height + \"|partial-touches:\" + __paintState.PartialDamageTouches + \"|full-touches:\" + __paintState.FullDamageTouches + \"|damage-merges:\" + __paintState.DamageMerges + \"|input-pending:\" + __PendingManagedInputCount() + \"|inputs-deferred:\" + global::System.Threading.Interlocked.Read(ref __paintState.InputsDeferred) + \"|inputs-drained:\" + global::System.Threading.Interlocked.Read(ref __paintState.InputsDrained) + \"|inputs-abandoned:\" + global::System.Threading.Interlocked.Read(ref __paintState.InputsAbandoned); } }");
            Add("private static global::System.Drawing.Rectangle __RegionDamage(global::System.Drawing.Region region) { var bounds = region.GetBounds(null!); var left = (int)global::System.Math.Floor(global::System.Math.Clamp((double)bounds.Left, int.MinValue, int.MaxValue)); var top = (int)global::System.Math.Floor(global::System.Math.Clamp((double)bounds.Top, int.MinValue, int.MaxValue)); var right = (int)global::System.Math.Ceiling(global::System.Math.Clamp((double)bounds.Right, int.MinValue, int.MaxValue)); var bottom = (int)global::System.Math.Ceiling(global::System.Math.Clamp((double)bounds.Bottom, int.MinValue, int.MaxValue)); return global::System.Drawing.Rectangle.FromLTRB(left, top, right, bottom); }");
            Add("private void __InvalidateCore(global::System.Drawing.Rectangle? damage, bool invalidateChildren) { if (__native.IsDisposed) return; var clipped = damage.HasValue ? __ClipManagedDamage(damage.Value) : ClientRectangle; if (clipped.Width <= 0 || clipped.Height <= 0) return; __TouchWindowSurface(); __QueueManagedPaint(damage, true); NotifyInvalidate(clipped); if (!invalidateChildren) return; foreach (Control child in Controls) { var overlap = global::System.Drawing.Rectangle.Intersect(clipped, child.Bounds); if (overlap.Width <= 0 || overlap.Height <= 0) continue; child.__InvalidateCore(new global::System.Drawing.Rectangle(overlap.X - child.Left, overlap.Y - child.Top, overlap.Width, overlap.Height), true); } }");
            Add("private void __InvalidateManagedPaintSurface() { global::System.Threading.Interlocked.Increment(ref __paintState.SurfaceEpoch); lock (__paintState.DamageGate) { __paintState.FullDamage = true; __paintState.PendingDamage = new global::System.Drawing.Rectangle(0, 0, Width, Height); __paintState.HasPendingDamage = Width > 0 && Height > 0; } if (__renderingManagedPaint) { __managedPaintSurfaceRetirePending = true; global::System.Threading.Interlocked.Exchange(ref __paintState.DirtyAfterRender, 1); return; } __ReleaseManagedPaintSurface(); }");
            Add("private void __ReleaseManagedPaintSurface() { var surface = __managedPaintSurface; __managedPaintSurface = null; __managedPaintSurfaceSize = global::System.Drawing.Size.Empty; __managedPaintSurfaceRetirePending = false; if (surface is null) return; surface.Dispose(); global::System.Threading.Interlocked.Increment(ref __paintState.SurfaceRetirements); }");
            Add("private global::System.Drawing.Bitmap __AcquireManagedPaintSurface(int width, int height, bool persistent) { if (!persistent) { global::System.Threading.Interlocked.Increment(ref __paintState.SurfaceAllocations); return new global::System.Drawing.Bitmap(width, height, global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb); } var requested = new global::System.Drawing.Size(width, height); if (__managedPaintSurface is not null && __managedPaintSurfaceSize == requested) { global::System.Threading.Interlocked.Increment(ref __paintState.SurfaceReuses); return __managedPaintSurface; } if (__managedPaintSurface is not null) { global::System.Threading.Interlocked.Increment(ref __paintState.SurfaceEpoch); __ReleaseManagedPaintSurface(); } __managedPaintSurface = new global::System.Drawing.Bitmap(width, height, global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb); __managedPaintSurfaceSize = requested; global::System.Threading.Interlocked.Increment(ref __paintState.SurfaceAllocations); return __managedPaintSurface; }");
            Add("private void __PaintManagedBackgroundImage(global::System.Drawing.Graphics graphics) { var image = __backgroundImage; if (image is null || image.Width <= 0 || image.Height <= 0) return; var client = ClientRectangle; switch (__backgroundImageLayout) { case ImageLayout.Tile: for (var y = 0; y < client.Height; y += image.Height) for (var x = 0; x < client.Width; x += image.Width) graphics.DrawImage(image, x, y); break; case ImageLayout.Center: graphics.DrawImage(image, new global::System.Drawing.Rectangle((client.Width - image.Width) / 2, (client.Height - image.Height) / 2, image.Width, image.Height)); break; case ImageLayout.Stretch: graphics.DrawImage(image, client); break; case ImageLayout.Zoom: var scale = global::System.Math.Min((double)client.Width / image.Width, (double)client.Height / image.Height); var width = global::System.Math.Max(1, (int)global::System.Math.Round(image.Width * scale)); var height = global::System.Math.Max(1, (int)global::System.Math.Round(image.Height * scale)); graphics.DrawImage(image, new global::System.Drawing.Rectangle((client.Width - width) / 2, (client.Height - height) / 2, width, height)); break; default: graphics.DrawImage(image, 0, 0); break; } }");
            Add("internal string __ManagedPaintSurfaceSnapshot() { var state = __native.IsDisposed ? \"retired\" : __renderingManagedPaint ? global::System.Threading.Volatile.Read(ref __paintState.DirtyAfterRender) != 0 ? \"rendering_dirty\" : \"rendering\" : global::System.Threading.Volatile.Read(ref __paintState.Queued) != 0 || global::System.Threading.Interlocked.Read(ref __paintState.RenderedRevision) < global::System.Threading.Interlocked.Read(ref __paintState.ContentRevision) ? \"dirty_queued\" : \"clean\"; return \"state:\" + state + \"|double-buffered:\" + (__DoubleBufferedRequested ? 1 : 0) + \"|surface:\" + (__managedPaintSurface is null ? 0 : 1) + \"|size:\" + __managedPaintSurfaceSize.Width + \"x\" + __managedPaintSurfaceSize.Height + \"|content:\" + global::System.Threading.Interlocked.Read(ref __paintState.ContentRevision) + \"|rendered:\" + global::System.Threading.Interlocked.Read(ref __paintState.RenderedRevision) + \"|epoch:\" + global::System.Threading.Interlocked.Read(ref __paintState.SurfaceEpoch) + \"|queued:\" + global::System.Threading.Volatile.Read(ref __paintState.Queued) + \"|dirty-after:\" + global::System.Threading.Volatile.Read(ref __paintState.DirtyAfterRender) + \"|queue-requests:\" + global::System.Threading.Interlocked.Read(ref __paintState.QueueRequests) + \"|queue-coalesced:\" + global::System.Threading.Interlocked.Read(ref __paintState.CoalescedRequests) + \"|leases-started:\" + global::System.Threading.Interlocked.Read(ref __paintState.LeasesStarted) + \"|leases-completed:\" + global::System.Threading.Interlocked.Read(ref __paintState.LeasesCompleted) + \"|leases-abandoned:\" + global::System.Threading.Interlocked.Read(ref __paintState.LeasesAbandoned) + \"|allocations:\" + global::System.Threading.Interlocked.Read(ref __paintState.SurfaceAllocations) + \"|reuses:\" + global::System.Threading.Interlocked.Read(ref __paintState.SurfaceReuses) + \"|retirements:\" + global::System.Threading.Interlocked.Read(ref __paintState.SurfaceRetirements) + \"|\" + __ManagedDamageSnapshot(); }");
            Add("private void __RenderManagedPaintNow() { if (__native.IsDisposed) return; if (__renderingManagedPaint) { __TouchManagedPaint(); return; } global::System.Threading.Interlocked.Exchange(ref __paintState.Queued, 0); if (global::System.Threading.Interlocked.Read(ref __paintState.RenderedRevision) >= global::System.Threading.Interlocked.Read(ref __paintState.ContentRevision)) return; __RenderManagedPaintWindows(); }");
            Add("private void __PostQueuedManagedPaint() { try { _ = BeginInvoke((global::System.Action)__RenderManagedPaintNow); } catch { global::System.Threading.Interlocked.Exchange(ref __paintState.Queued, 0); throw; } }");
            Add("private void __QueueManagedPaint() { __QueueManagedPaint(null, true); }");
            Add("private void __QueueManagedPaint(global::System.Drawing.Rectangle damage) { __QueueManagedPaint(damage, true); }");
            Add("private void __QueueManagedPaint(bool touch) { __QueueManagedPaint(null, touch); }");
            Add("private void __QueueManagedPaint(global::System.Drawing.Rectangle? damage, bool touch) { if (__native.IsDisposed) return; if (touch && !__TouchManagedPaint(damage)) return; var form = FindForm(); if (!Visible || (form is not null && !form.Visible)) { global::System.Threading.Interlocked.Exchange(ref __paintState.Queued, 0); return; } if (!__loadRaised || form is null) { __RenderManagedPaintWindows(); return; } if (global::System.Threading.Interlocked.Exchange(ref __paintState.Queued, 1) != 0) { global::System.Threading.Interlocked.Increment(ref __paintState.CoalescedRequests); return; } global::System.Threading.Interlocked.Increment(ref __paintState.QueueRequests); if (NativeControlBridge.InNativeCallback) { __QueueCallbackPaint(this); return; } __PostQueuedManagedPaint(); }");
            Add("private void __RenderManagedPaintWindows() { if (__native.IsDisposed || !__native.SupportsRaster || Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096) return; if (__renderingManagedPaint) { global::System.Threading.Interlocked.Exchange(ref __paintState.DirtyAfterRender, 1); return; } var paintWidth = Width; var paintHeight = Height; var paintBounds = new global::System.Drawing.Rectangle(0, 0, paintWidth, paintHeight); var persistent = __DoubleBufferedRequested; var paintDamage = __TakeManagedDamage(paintWidth, paintHeight, persistent); var leaseDamageFull = paintDamage == paintBounds; var telemetryStarted = FacadeCallTelemetry.IsEnabled ? global::System.Diagnostics.Stopwatch.GetTimestamp() : 0L; long telemetryPngBytes = 0; long telemetrySurfaceBytes = 0; global::System.Drawing.Bitmap? bitmap = null; var abandoned = false; __renderingManagedPaint = true; global::System.Threading.Interlocked.Increment(ref __paintState.LeasesStarted); try { bitmap = __AcquireManagedPaintSurface(paintWidth, paintHeight, persistent); var leaseEpoch = global::System.Threading.Interlocked.Read(ref __paintState.SurfaceEpoch); var leaseRevision = global::System.Threading.Interlocked.Read(ref __paintState.ContentRevision); using (var graphics = global::System.Drawing.Graphics.FromImage(bitmap)) { graphics.SetClip(paintDamage); var paintArgs = new PaintEventArgs(graphics, paintDamage); if ((__styles & (long)ControlStyles.Opaque) == 0) OnPaintBackground(paintArgs); OnPaint(paintArgs); } if (__native.IsDisposed) { abandoned = true; global::System.Threading.Interlocked.Increment(ref __paintState.LeasesAbandoned); return; } if (leaseEpoch != global::System.Threading.Interlocked.Read(ref __paintState.SurfaceEpoch) || Width != paintWidth || Height != paintHeight) { abandoned = true; global::System.Threading.Interlocked.Increment(ref __paintState.LeasesAbandoned); __RestoreManagedDamage(paintDamage, leaseDamageFull); global::System.Threading.Interlocked.Exchange(ref __paintState.DirtyAfterRender, 1); return; } var captureDirectory = global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_CAPTURE_RASTER_DIR\"); if (this is ContextMenuStrip && !global::System.String.IsNullOrWhiteSpace(captureDirectory)) { using var stream = new global::System.IO.MemoryStream(); bitmap.Save(stream, global::System.Drawing.Imaging.ImageFormat.Png); var png = stream.ToArray(); telemetryPngBytes = png.Length; global::System.IO.Directory.CreateDirectory(captureDirectory); var sequence = global::System.Threading.Interlocked.Increment(ref __paintCaptureSequence); var capturePath = global::System.IO.Path.Combine(captureDirectory, \"context-menu-\" + sequence.ToString(\"D4\", global::System.Globalization.CultureInfo.InvariantCulture) + \".png\"); global::System.IO.File.WriteAllBytes(capturePath, png); global::System.Console.Error.WriteLine(\"facade-paint-capture=\" + capturePath); } var bitmapData = bitmap.LockBits(paintBounds, global::System.Drawing.Imaging.ImageLockMode.ReadOnly, global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb); try { telemetrySurfaceBytes = checked((long)bitmapData.Stride * paintHeight); __TraceManagedPaintContent(bitmapData, paintWidth, paintHeight); __native.SetRasterPixels(bitmapData.Scan0, paintWidth, paintHeight, bitmapData.Stride); } finally { bitmap.UnlockBits(bitmapData); } lock (__paintState.DamageGate) __paintState.LastRenderedDamage = paintDamage; global::System.Threading.Interlocked.Exchange(ref __paintState.RenderedRevision, leaseRevision); global::System.Threading.Interlocked.Increment(ref __paintState.LeasesCompleted); var typeName = GetType().FullName ?? GetType().Name; if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_PAINT\") == \"1\" && __paintTrace.TryAdd(typeName, 0)) global::System.Console.Error.WriteLine(\"facade-paint=type:\" + typeName + \"|size:\" + paintWidth + \"x\" + paintHeight + \"|damage:\" + paintDamage.X + \",\" + paintDamage.Y + \",\" + paintDamage.Width + \",\" + paintDamage.Height + \"|surface:\" + telemetrySurfaceBytes); } catch (global::System.Exception error) { if (!abandoned) { global::System.Threading.Interlocked.Increment(ref __paintState.LeasesAbandoned); if (!__native.IsDisposed) __RestoreManagedDamage(paintDamage, leaseDamageFull); } Application.__ReportCallbackException(error); } finally { if (FacadeCallTelemetry.IsEnabled) { FacadeCallTelemetry.ObserveValue(\"managed-paint.nanoseconds\", checked(global::System.Diagnostics.Stopwatch.GetElapsedTime(telemetryStarted).Ticks * 100L)); FacadeCallTelemetry.ObserveValue(\"managed-paint.png-bytes\", telemetryPngBytes); FacadeCallTelemetry.ObserveValue(\"managed-paint.surface-bytes\", telemetrySurfaceBytes); FacadeCallTelemetry.Observe(\"managed-paint.surface-policy\", persistent ? \"persistent\" : \"ephemeral\"); } __renderingManagedPaint = false; if (__managedPaintSurfaceRetirePending) __ReleaseManagedPaintSurface(); if (!persistent) bitmap?.Dispose(); if (global::System.Threading.Interlocked.Exchange(ref __paintState.DirtyAfterRender, 0) != 0 && !__native.IsDisposed) __QueueManagedPaint(false); } }");
            Add("private unsafe void __TraceManagedPaintContent(global::System.Drawing.Imaging.BitmapData data, int width, int height) { if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_PAINT_CONTENT\") != \"1\") return; var key = (GetType().FullName ?? GetType().Name) + \"|\" + Name; var sequence = __paintContentTrace.AddOrUpdate(key, 1, static (_, prior) => prior + 1); if (sequence > 4) return; var pixels = (byte*)data.Scan0; var total = checked((long)width * height); var step = global::System.Math.Max(1, (int)global::System.Math.Sqrt(global::System.Math.Max(1d, total / 4096d))); var minB = 255; var minG = 255; var minR = 255; var minA = 255; var maxB = 0; var maxG = 0; var maxR = 0; var maxA = 0; long samples = 0; long differing = 0; uint first = 0; var haveFirst = false; for (var y = 0; y < height; y += step) { var row = pixels + checked(y * data.Stride); for (var x = 0; x < width; x += step) { var pixel = row + checked(x * 4); var b = pixel[0]; var g = pixel[1]; var r = pixel[2]; var a = pixel[3]; minB = global::System.Math.Min(minB, b); maxB = global::System.Math.Max(maxB, b); minG = global::System.Math.Min(minG, g); maxG = global::System.Math.Max(maxG, g); minR = global::System.Math.Min(minR, r); maxR = global::System.Math.Max(maxR, r); minA = global::System.Math.Min(minA, a); maxA = global::System.Math.Max(maxA, a); var packed = (uint)(b | g << 8 | r << 16 | a << 24); if (!haveFirst) { first = packed; haveFirst = true; } else if (packed != first) ++differing; ++samples; } } global::System.Console.Error.WriteLine(\"gui-forms-paint-content=type:\" + (GetType().FullName ?? GetType().Name) + \"|name:\" + Name + \"|size:\" + width + \"x\" + height + \"|sequence:\" + sequence + \"|back:\" + BackColor.ToArgb().ToString(\"X8\", global::System.Globalization.CultureInfo.InvariantCulture) + \"|b:\" + minB + \"-\" + maxB + \"|g:\" + minG + \"-\" + maxG + \"|r:\" + minR + \"-\" + maxR + \"|a:\" + minA + \"-\" + maxA + \"|different:\" + differing + \"/\" + samples); }");
            Add("private void __ApplyDockLayout() { var remaining = new global::System.Drawing.Rectangle(__padding.Left, __padding.Top, global::System.Math.Max(0, ClientSize.Width - __padding.Left - __padding.Right), global::System.Math.Max(0, ClientSize.Height - __padding.Top - __padding.Bottom)); for (var index = __controls.Count - 1; index >= 0; --index) { var child = __controls[index]; if (!child.Visible) continue; var bounds = child.Bounds; switch (child.Dock) { case DockStyle.Top: bounds = new global::System.Drawing.Rectangle(remaining.X, remaining.Y, remaining.Width, bounds.Height); remaining.Y += bounds.Height; remaining.Height = global::System.Math.Max(0, remaining.Height - bounds.Height); break; case DockStyle.Bottom: bounds = new global::System.Drawing.Rectangle(remaining.X, remaining.Bottom - bounds.Height, remaining.Width, bounds.Height); remaining.Height = global::System.Math.Max(0, remaining.Height - bounds.Height); break; case DockStyle.Left: bounds = new global::System.Drawing.Rectangle(remaining.X, remaining.Y, bounds.Width, remaining.Height); remaining.X += bounds.Width; remaining.Width = global::System.Math.Max(0, remaining.Width - bounds.Width); break; case DockStyle.Right: bounds = new global::System.Drawing.Rectangle(remaining.Right - bounds.Width, remaining.Y, bounds.Width, remaining.Height); remaining.Width = global::System.Math.Max(0, remaining.Width - bounds.Width); break; case DockStyle.Fill: bounds = remaining; break; default: continue; } child.__SetLayoutBounds(bounds); } }");
            Add("internal void __SetParent(Control? value) { if (global::System.Object.ReferenceEquals(__parent, value)) return; if (__parent is not null) __parent.Controls.Remove(this); if (value is not null) value.Controls.Add(this); }");
            Add("internal global::System.Drawing.Point __ScreenOffset() { var point = Location; for (var current = __parent; current is not null; current = current.__parent) point.Offset(current.Location); return point; }");
            Add("internal global::System.Drawing.Point __HostOffset() { var point = Location; for (var current = __parent; current is not null && current is not Form; current = current.__parent) point.Offset(current.Location); return point; }");
            Add("internal void __SynchronizeWindowSurfaceTree() { if (__native.HasWindowHandle) __native.ConfigureWindowSurface(__HostOffset(), ClientSize); foreach (Control child in Controls) child.__SynchronizeWindowSurfaceTree(); }");
            Add("internal bool __ContainsDescendant(Control candidate) { foreach (Control child in Controls) if (global::System.Object.ReferenceEquals(child, candidate) || child.__ContainsDescendant(candidate)) return true; return false; }");
            Add("internal bool __CanFocus { get { return (this is not Form form || !form.TopLevel) && (this is not Label || this is LinkLabel) && this is not Panel && this is not GroupBox && this is not PictureBox && this is not ProgressBar && this is not Splitter; } }");
            Add("internal void __CollectTabCandidates(global::System.Collections.Generic.List<Control> target) { var ordered = new global::System.Collections.Generic.List<(Control Control, int Order)>(); var order = 0; for (var index = Controls.Count - 1; index >= 0; --index) ordered.Add((Controls[index], order++)); ordered.Sort((left, right) => { var compared = left.Control.__TabIndexValue.CompareTo(right.Control.__TabIndexValue); return compared != 0 ? compared : left.Order.CompareTo(right.Order); }); foreach (var entry in ordered) { var child = entry.Control; if (child.__CanFocus && child.__tabStop && child.Enabled && child.Visible && !child.__native.IsDisposed) target.Add(child); child.__CollectTabCandidates(target); } }");
            Add("internal void __CollectTabOrder(global::System.Collections.Generic.List<Control> target) { var ordered = new global::System.Collections.Generic.List<(Control Control, int Order)>(); var order = 0; for (var index = Controls.Count - 1; index >= 0; --index) ordered.Add((Controls[index], order++)); ordered.Sort((left, right) => { var compared = left.Control.__TabIndexValue.CompareTo(right.Control.__TabIndexValue); return compared != 0 ? compared : left.Order.CompareTo(right.Order); }); foreach (var entry in ordered) { var child = entry.Control; if (child.__native.IsDisposed) continue; target.Add(child); child.__CollectTabOrder(target); } }");
            Add("internal bool __SelectNextDescendant(Control? current, bool forward) { var candidates = new global::System.Collections.Generic.List<Control>(); __CollectTabCandidates(candidates); if (candidates.Count == 0) return false; var index = current is null ? (forward ? -1 : 0) : candidates.IndexOf(current); for (var offset = 1; offset <= candidates.Count; ++offset) { var next = forward ? index + offset : index - offset; next = (next % candidates.Count + candidates.Count) % candidates.Count; if (candidates[next].Focus()) return true; } return false; }");
            Add("internal static void __ClearFocusWithin(Control scope) { var focused = __focusedControl; if (focused is null || (!global::System.Object.ReferenceEquals(focused, scope) && !scope.__ContainsDescendant(focused))) return; __focusedControl = null; focused.OnLostFocus(global::System.EventArgs.Empty); focused.Leave?.Invoke(focused, global::System.EventArgs.Empty); }");
            Add("internal static void __RestoreFocus(Control? control) { if (control is not null) control.Focus(); }");
            Add("internal bool __GetStyle(ControlStyles style) { return (__styles & (long)style) == (long)style; }");
            Add("internal void __SetStyle(ControlStyles style, bool enabled) { var bits = (long)style; var previous = __styles; if (enabled) __styles |= bits; else __styles &= ~bits; if (previous == __styles) return; if ((bits & (long)ControlStyles.OptimizedDoubleBuffer) != 0) { __doubleBuffered = enabled; if (!enabled) __InvalidateManagedPaintSurface(); } if (__loadRaised) __QueueManagedPaint(); }");
            Add("internal Padding __LayoutMargin { get { return __margin; } }");
            Add("internal global::System.Drawing.Size __ExplicitPreferredSize { get { return __preferredSize; } }");
            Add("internal bool __TabStop { get { return __tabStop; } }");
            Add("internal int __TabIndexValue { get { return __tabIndex; } }");
            Add("internal bool __LayoutAutoSize { get { return __autoSize; } }");
            Add("internal global::System.Drawing.Size __PreferredLayoutSize() { var authoredWidth = __preferredSize.Width; var authoredHeight = __preferredSize.Height; var intrinsicWidth = __padding.Left + __padding.Right; var intrinsicHeight = __padding.Top + __padding.Bottom; if (__autoSize && this is TableLayoutPanel table) { var tableSize = table.__PreferredTableLayoutSize(); intrinsicWidth = global::System.Math.Max(intrinsicWidth, tableSize.Width); intrinsicHeight = global::System.Math.Max(intrinsicHeight, tableSize.Height); } if (this is Label && __autoSize && !global::System.String.IsNullOrEmpty(Text)) { var measured = TextRenderer.MeasureText(Text, Font); intrinsicWidth = global::System.Math.Max(intrinsicWidth, measured.Width); intrinsicHeight = global::System.Math.Max(intrinsicHeight, Font.Height); } if (this is PictureBox picture && picture.__ProjectionPictureImage is global::System.Drawing.Image image && (picture.__ProjectionPictureSizeMode == PictureBoxSizeMode.AutoSize || authoredWidth == 0 || authoredHeight == 0)) { intrinsicWidth = global::System.Math.Max(intrinsicWidth, image.Width); intrinsicHeight = global::System.Math.Max(intrinsicHeight, image.Height); } if (this is not TableLayoutPanel) foreach (Control child in Controls) { if (!child.Visible) continue; var preferred = child.__PreferredLayoutSize(); var margin = child.Dock == DockStyle.None ? child.__LayoutMargin : new Padding(0); intrinsicWidth = global::System.Math.Max(intrinsicWidth, child.Left + preferred.Width + margin.Right + __padding.Right); intrinsicHeight = global::System.Math.Max(intrinsicHeight, child.Top + preferred.Height + margin.Bottom + __padding.Bottom); } var width = __autoSize ? (__autoSizeMode == AutoSizeMode.GrowOnly ? global::System.Math.Max(authoredWidth, intrinsicWidth) : intrinsicWidth) : (authoredWidth > 0 ? authoredWidth : intrinsicWidth); var height = __autoSize ? (__autoSizeMode == AutoSizeMode.GrowOnly ? global::System.Math.Max(authoredHeight, intrinsicHeight) : intrinsicHeight) : (authoredHeight > 0 ? authoredHeight : intrinsicHeight); width = global::System.Math.Max(width, __minimumSize.Width); height = global::System.Math.Max(height, __minimumSize.Height); if (__maximumSize.Width > 0) width = global::System.Math.Min(width, __maximumSize.Width); if (__maximumSize.Height > 0) height = global::System.Math.Min(height, __maximumSize.Height); return new global::System.Drawing.Size(width, height); }");
            Add("internal void __RefreshAutoSizeFromChildren() { if (!__autoSize) return; var preferred = __PreferredLayoutSize(); var preferredChanged = !__hasAutoSizePreferred || preferred != __lastAutoSizePreferred; __lastAutoSizePreferred = preferred; __hasAutoSizePreferred = true; var next = Size; if (__parent is null || __dock is not DockStyle.Top and not DockStyle.Bottom and not DockStyle.Fill) next.Width = __autoSizeMode == AutoSizeMode.GrowOnly ? global::System.Math.Max(__preferredSize.Width, preferred.Width) : preferred.Width; if (__parent is null || __dock is not DockStyle.Left and not DockStyle.Right and not DockStyle.Fill) next.Height = __autoSizeMode == AutoSizeMode.GrowOnly ? global::System.Math.Max(__preferredSize.Height, preferred.Height) : preferred.Height; if (next != Size) __SetLayoutBounds(new global::System.Drawing.Rectangle(Location, next)); if (preferredChanged) __parent?.PerformLayout(); }");
            Add("public event global::System.EventHandler? BackColorChanged;");
            Add("public event global::System.EventHandler? EnabledChanged;");
            Add("public event global::System.EventHandler? FontChanged;");
            Add("public event global::System.EventHandler? ForeColorChanged;");
            Add("public event global::System.EventHandler? GotFocus;");
            Add("protected virtual void OnBackColorChanged(global::System.EventArgs e) { BackColorChanged?.Invoke(this, e); }");
            Add("protected virtual void OnTextChanged(global::System.EventArgs e) { TextChanged?.Invoke(this, e); }");
            Add("protected virtual void OnVisibleChanged(global::System.EventArgs e) { VisibleChanged?.Invoke(this, e); }");
            break;
        case "System.Windows.Forms.ContainerControl":
            Add("private global::System.Drawing.SizeF __autoScaleDimensions;");
            Add("private AutoScaleMode __autoScaleMode;");
            break;
        case "System.Windows.Forms.TableLayoutStyle":
            Add("protected SizeType __sizeType;");
            Add("protected float __size;");
            Add("internal SizeType __LayoutSizeType { get { return __sizeType; } }");
            Add("internal float __LayoutSize { get { return __size; } }");
            break;
        case "System.Windows.Forms.TableLayoutStyleCollection":
            Add("protected readonly global::System.Collections.Generic.List<TableLayoutStyle> __styles = new();");
            Add("internal global::System.Collections.Generic.IReadOnlyList<TableLayoutStyle> __LayoutStyles { get { return __styles; } }");
            break;
        case "System.Windows.Forms.TableLayoutPanel":
            Add("private readonly TableLayoutControlCollection __tableControls;");
            Add("private readonly TableLayoutColumnStyleCollection __columnStyles = new();");
            Add("private readonly TableLayoutRowStyleCollection __rowStyles = new();");
            Add("private readonly global::System.Collections.Generic.Dictionary<Control, int> __columnSpans = new();");
            Add("private readonly global::System.Collections.Generic.Dictionary<Control, int> __rowSpans = new();");
            Add("private int __columnCount;");
            Add("private int __rowCount;");
            Add("private readonly global::System.Collections.Generic.Dictionary<Control, (int Column, int Row)> __cells = new();");
            Add("internal void __SetCell(Control control, int column, int row) { if (column < -1) throw new global::System.ArgumentOutOfRangeException(nameof(column)); if (row < -1) throw new global::System.ArgumentOutOfRangeException(nameof(row)); __cells[control] = (column, row); }");
            Add("internal string __TableTrace() { var columns = global::System.String.Join(',', global::System.Linq.Enumerable.Select(__columnStyles.__LayoutStyles, style => style.__LayoutSizeType + \":\" + style.__LayoutSize)); var rows = global::System.String.Join(',', global::System.Linq.Enumerable.Select(__rowStyles.__LayoutStyles, style => style.__LayoutSizeType + \":\" + style.__LayoutSize)); var cells = global::System.String.Join(',', global::System.Linq.Enumerable.Select(__cells, pair => pair.Key.Name + \":\" + pair.Value.Column + \":\" + pair.Value.Row)); return \"|table-columns=\" + columns + \"|table-rows=\" + rows + \"|table-cells=\" + cells; }");
            Add("private static int[] __PreferredTableAxis(int count, global::System.Collections.Generic.IReadOnlyList<TableLayoutStyle> styles, bool columns, TableLayoutPanel owner) { count = global::System.Math.Max(1, count); var sizes = new int[count]; for (var i = 0; i < count; ++i) { var style = i < styles.Count ? styles[i] : null; if (style?.__LayoutSizeType == SizeType.Absolute) sizes[i] = global::System.Math.Max(0, (int)global::System.Math.Round(style.__LayoutSize)); } var next = 0; foreach (Control child in owner.Controls) { if (!child.Visible) continue; var cell = owner.__cells.TryGetValue(child, out var assigned) ? assigned : (Column: -1, Row: -1); var column = cell.Column < 0 ? next % global::System.Math.Max(1, owner.__columnCount) : global::System.Math.Min(cell.Column, global::System.Math.Max(1, owner.__columnCount) - 1); var row = cell.Row < 0 ? next / global::System.Math.Max(1, owner.__columnCount) : global::System.Math.Min(cell.Row, global::System.Math.Max(1, owner.__rowCount) - 1); ++next; row = global::System.Math.Min(row, global::System.Math.Max(1, owner.__rowCount) - 1); var index = columns ? column : row; var span = columns ? (owner.__columnSpans.TryGetValue(child, out var columnSpan) ? columnSpan : 1) : (owner.__rowSpans.TryGetValue(child, out var rowSpan) ? rowSpan : 1); span = global::System.Math.Min(global::System.Math.Max(1, span), count - index); var margin = child.__LayoutMargin; var preferred = child.__PreferredLayoutSize(); var desired = columns ? preferred.Width + margin.Left + margin.Right : preferred.Height + margin.Top + margin.Bottom; var allocated = 0; for (var i = index; i < index + span; ++i) allocated += sizes[i]; if (allocated < desired) { var target = index + span - 1; for (var i = target; i >= index; --i) { var style = i < styles.Count ? styles[i] : null; if (style?.__LayoutSizeType != SizeType.Absolute) { target = i; break; } } sizes[target] += desired - allocated; } } return sizes; }");
            Add("internal global::System.Drawing.Size __PreferredTableLayoutSize() { var widths = __PreferredTableAxis(global::System.Math.Max(1, __columnCount), __columnStyles.__LayoutStyles, true, this); var heights = __PreferredTableAxis(global::System.Math.Max(1, __rowCount), __rowStyles.__LayoutStyles, false, this); var width = Padding.Left + Padding.Right; var height = Padding.Top + Padding.Bottom; foreach (var value in widths) width += value; foreach (var value in heights) height += value; return new global::System.Drawing.Size(width, height); }");
            Add("private static int[] __TableAxis(int total, int count, global::System.Collections.Generic.IReadOnlyList<TableLayoutStyle> styles, bool columns, TableLayoutPanel owner) { count = global::System.Math.Max(1, count); total = global::System.Math.Max(0, total); var sizes = new int[count]; var remaining = total; var percent = 0f; var percentCells = 0; for (var i = 0; i < count; ++i) { var style = i < styles.Count ? styles[i] : null; if (style?.__LayoutSizeType == SizeType.Absolute) { sizes[i] = global::System.Math.Min(remaining, global::System.Math.Max(0, (int)global::System.Math.Round(style.__LayoutSize))); remaining -= sizes[i]; } else if (style?.__LayoutSizeType == SizeType.Percent) { percent += global::System.Math.Max(0f, style.__LayoutSize); ++percentCells; } } for (var i = 0; i < count && remaining > 0; ++i) { var style = i < styles.Count ? styles[i] : null; if (style?.__LayoutSizeType == SizeType.Absolute || style?.__LayoutSizeType == SizeType.Percent) continue; var desired = 0; foreach (Control child in owner.Controls) { if (!child.Visible) continue; if (owner.__cells.TryGetValue(child, out var cell) && (columns ? cell.Column : cell.Row) == i && (columns ? (!owner.__columnSpans.TryGetValue(child, out var columnSpan) || columnSpan == 1) : (!owner.__rowSpans.TryGetValue(child, out var rowSpan) || rowSpan == 1))) { var margin = child.__LayoutMargin; var preferred = child.__PreferredLayoutSize(); desired = global::System.Math.Max(desired, columns ? preferred.Width + margin.Left + margin.Right : preferred.Height + margin.Top + margin.Bottom); } } sizes[i] = global::System.Math.Min(remaining, global::System.Math.Max(0, desired)); remaining -= sizes[i]; } var percentRemaining = remaining; var lastPercent = -1; for (var i = 0; i < count; ++i) { var style = i < styles.Count ? styles[i] : null; if (style?.__LayoutSizeType != SizeType.Percent) continue; lastPercent = i; var share = percent <= 0f ? (percentCells == 0 ? 0 : percentRemaining / percentCells) : (int)global::System.Math.Floor(percentRemaining * global::System.Math.Max(0f, style.__LayoutSize) / percent); share = global::System.Math.Min(remaining, global::System.Math.Max(0, share)); sizes[i] = share; remaining -= share; } if (remaining > 0) sizes[lastPercent >= 0 ? lastPercent : count - 1] += remaining; return sizes; }");
            Add("private void __ApplyTableLayout() { var columns = global::System.Math.Max(1, __columnCount); var rows = global::System.Math.Max(1, __rowCount); var widths = __TableAxis(ClientSize.Width - Padding.Left - Padding.Right, columns, __columnStyles.__LayoutStyles, true, this); var heights = __TableAxis(ClientSize.Height - Padding.Top - Padding.Bottom, rows, __rowStyles.__LayoutStyles, false, this); var next = 0; foreach (Control child in Controls) { if (!child.Visible) continue; var cell = __cells.TryGetValue(child, out var assigned) ? assigned : (Column: -1, Row: -1); var column = cell.Column < 0 ? next % columns : global::System.Math.Min(cell.Column, columns - 1); var row = cell.Row < 0 ? next / columns : global::System.Math.Min(cell.Row, rows - 1); ++next; row = global::System.Math.Min(row, rows - 1); var columnSpan = global::System.Math.Min(__columnSpans.TryGetValue(child, out var cs) ? cs : 1, columns - column); var rowSpan = global::System.Math.Min(__rowSpans.TryGetValue(child, out var rs) ? rs : 1, rows - row); var x = Padding.Left; for (var i = 0; i < column; ++i) x += widths[i]; var y = Padding.Top; for (var i = 0; i < row; ++i) y += heights[i]; var width = 0; for (var i = column; i < column + columnSpan; ++i) width += widths[i]; var height = 0; for (var i = row; i < row + rowSpan; ++i) height += heights[i]; var margin = child.__LayoutMargin; var cellBounds = new global::System.Drawing.Rectangle(x + margin.Left, y + margin.Top, global::System.Math.Max(0, width - margin.Left - margin.Right), global::System.Math.Max(0, height - margin.Top - margin.Bottom)); var bounds = child.Bounds; var preferred = child.__PreferredLayoutSize(); if (child.__LayoutAutoSize || bounds.Width == 0) bounds.Width = preferred.Width; if (child.__LayoutAutoSize || bounds.Height == 0) bounds.Height = preferred.Height; if (child.Dock is DockStyle.Fill or DockStyle.Top or DockStyle.Bottom || (child.Anchor & (AnchorStyles.Left | AnchorStyles.Right)) == (AnchorStyles.Left | AnchorStyles.Right)) bounds.Width = cellBounds.Width; if (child.Dock is DockStyle.Fill or DockStyle.Left or DockStyle.Right || (child.Anchor & (AnchorStyles.Top | AnchorStyles.Bottom)) == (AnchorStyles.Top | AnchorStyles.Bottom)) bounds.Height = cellBounds.Height; bounds.Width = global::System.Math.Min(bounds.Width, cellBounds.Width); bounds.Height = global::System.Math.Min(bounds.Height, cellBounds.Height); bounds.X = child.Dock == DockStyle.Right || ((child.Anchor & AnchorStyles.Right) != 0 && (child.Anchor & AnchorStyles.Left) == 0) ? cellBounds.Right - bounds.Width : child.Dock is DockStyle.Fill or DockStyle.Top or DockStyle.Bottom or DockStyle.Left || (child.Anchor & AnchorStyles.Left) != 0 ? cellBounds.Left : cellBounds.Left + (cellBounds.Width - bounds.Width) / 2; bounds.Y = child.Dock == DockStyle.Bottom || ((child.Anchor & AnchorStyles.Bottom) != 0 && (child.Anchor & AnchorStyles.Top) == 0) ? cellBounds.Bottom - bounds.Height : child.Dock is DockStyle.Fill or DockStyle.Top or DockStyle.Left or DockStyle.Right || (child.Anchor & AnchorStyles.Top) != 0 ? cellBounds.Top : cellBounds.Top + (cellBounds.Height - bounds.Height) / 2; child.__SetLayoutBounds(bounds); } }");
            Add("protected override void OnLayout(LayoutEventArgs levent) { if (__LayoutAutoSize && !__assigningLayoutBounds) __RefreshAutoSizeFromChildren(); __ApplyTableLayout(); }");
            break;
        case "System.Windows.Forms.TableLayoutControlCollection":
            Add("private readonly TableLayoutPanel? __tableOwner;");
            Add("internal TableLayoutControlCollection(Control owner) : base(owner) { __tableOwner = owner as TableLayoutPanel; }");
            break;
        case "System.Windows.Forms.FlowLayoutPanel":
            Add("private bool __wrapContents = true;");
            Add("private void __ApplyFlowLayout() { var x = Padding.Left; var y = Padding.Top; var lineHeight = 0; var right = global::System.Math.Max(Padding.Left, ClientSize.Width - Padding.Right); foreach (Control child in Controls) { if (!child.Visible) continue; var margin = child.__LayoutMargin; var width = child.Width; var height = child.Height; if (__wrapContents && x > Padding.Left && x + margin.Left + width + margin.Right > right) { x = Padding.Left; y += lineHeight; lineHeight = 0; } child.__SetLayoutBounds(new global::System.Drawing.Rectangle(x + margin.Left, y + margin.Top, width, height)); x += margin.Left + width + margin.Right; lineHeight = global::System.Math.Max(lineHeight, margin.Top + height + margin.Bottom); } }");
            Add("protected override void OnLayout(LayoutEventArgs levent) { __ApplyFlowLayout(); }");
            break;
        case "System.Windows.Forms.CheckBox":
            Add("private CheckState __checkState;");
            Add("private Appearance __checkAppearance;");
            Add("private global::System.Drawing.ContentAlignment __checkAlignment = global::System.Drawing.ContentAlignment.MiddleLeft;");
            break;
        case "System.Windows.Forms.ButtonBase":
            Add("private readonly FlatButtonAppearance __flatAppearance = new();");
            Add("private FlatStyle __buttonFlatStyle = FlatStyle.Standard;");
            Add("private global::System.Drawing.ContentAlignment __buttonTextAlign = global::System.Drawing.ContentAlignment.MiddleCenter;");
            Add("private bool __useVisualStyleBackColor = true;");
            Add("internal void __ApplyNativeButtonAppearance() { var nativeStyle = __buttonFlatStyle is FlatStyle.Flat or FlatStyle.Popup ? 1u : 0u; if (__native.SupportsRaster) { Invalidate(); return; } if (this is Button) __native.SetButtonAppearance(nativeStyle, __flatAppearance.__BorderSize); }");
            Add("private void __PaintButtonSurface(global::System.Drawing.Graphics graphics) { using var background = new global::System.Drawing.SolidBrush(BackColor); graphics.FillRectangle(background, ClientRectangle); var borderSize = __buttonFlatStyle is FlatStyle.Flat or FlatStyle.Popup ? __flatAppearance.__BorderSize : 1; if (borderSize > 0 && Width > borderSize && Height > borderSize) { using var top = new global::System.Drawing.Pen(global::System.Drawing.Color.FromArgb(110, global::System.Drawing.Color.White), borderSize); using var edge = new global::System.Drawing.Pen(global::System.Drawing.Color.FromArgb(150, global::System.Drawing.Color.Black), borderSize); var inset = borderSize / 2f; graphics.DrawLine(top, inset, inset, Width - inset, inset); graphics.DrawLine(top, inset, inset, inset, Height - inset); graphics.DrawLine(edge, inset, Height - inset, Width - inset, Height - inset); graphics.DrawLine(edge, Width - inset, inset, Width - inset, Height - inset); } if (this is Button button && button.__IsDefaultButton && Width > 3 && Height > 3) { using var cue = new global::System.Drawing.Pen(global::System.Drawing.Color.FromArgb(62, 128, 184)); graphics.DrawRectangle(cue, 1, 1, Width - 3, Height - 3); } var image = __ProjectionBackgroundImage; if (image is not null) { var destination = __ProjectionBackgroundImageLayout switch { ImageLayout.Stretch => ClientRectangle, ImageLayout.Zoom => __ZoomImage(image), ImageLayout.Center => new global::System.Drawing.Rectangle((Width - image.Width) / 2, (Height - image.Height) / 2, image.Width, image.Height), _ => new global::System.Drawing.Rectangle(2, 2, global::System.Math.Min(image.Width, global::System.Math.Max(0, Width - 4)), global::System.Math.Min(image.Height, global::System.Math.Max(0, Height - 4))) }; if (destination.Width > 0 && destination.Height > 0) graphics.DrawImage(image, destination); } else if (!global::System.String.IsNullOrEmpty(Text)) { using var ink = new global::System.Drawing.SolidBrush(ForeColor); using var font = new global::System.Drawing.Font(\"Portsmouth Rapids\", 10f, global::System.Drawing.FontStyle.Regular, global::System.Drawing.GraphicsUnit.Pixel, 1); var measured = graphics.MeasureString(Text, font); var textX = __buttonTextAlign is global::System.Drawing.ContentAlignment.TopLeft or global::System.Drawing.ContentAlignment.MiddleLeft or global::System.Drawing.ContentAlignment.BottomLeft ? 3f : __buttonTextAlign is global::System.Drawing.ContentAlignment.TopRight or global::System.Drawing.ContentAlignment.MiddleRight or global::System.Drawing.ContentAlignment.BottomRight ? global::System.Math.Max(3f, Width - measured.Width - 3f) : global::System.Math.Max(3f, (Width - measured.Width) / 2f); var textY = __buttonTextAlign is global::System.Drawing.ContentAlignment.TopLeft or global::System.Drawing.ContentAlignment.TopCenter or global::System.Drawing.ContentAlignment.TopRight ? 2f : __buttonTextAlign is global::System.Drawing.ContentAlignment.BottomLeft or global::System.Drawing.ContentAlignment.BottomCenter or global::System.Drawing.ContentAlignment.BottomRight ? global::System.Math.Max(2f, Height - measured.Height - 2f) : global::System.Math.Max(2f, (Height - measured.Height) / 2f); graphics.DrawString(Text, font, ink, textX, textY); } }");
            Add("protected global::System.Drawing.Rectangle __ZoomImage(global::System.Drawing.Image image) { if (image.Width <= 0 || image.Height <= 0 || Width <= 4 || Height <= 4) return global::System.Drawing.Rectangle.Empty; var scale = global::System.Math.Min((Width - 6d) / image.Width, (Height - 6d) / image.Height); var width = global::System.Math.Max(1, (int)global::System.Math.Round(image.Width * scale)); var height = global::System.Math.Max(1, (int)global::System.Math.Round(image.Height * scale)); return new global::System.Drawing.Rectangle((Width - width) / 2, (Height - height) / 2, width, height); }");
            break;
        case "System.Windows.Forms.Button":
            Add("private DialogResult __buttonDialogResult;");
            Add("private bool __buttonIsDefault;");
            Add("internal bool __IsDefaultButton { get { return __buttonIsDefault; } }");
            Add("public void NotifyDefault(bool value) { if (__buttonIsDefault == value) return; __buttonIsDefault = value; Invalidate(); }");
            Add("public void PerformClick() { if (Enabled && Visible) __NativeEvent(NativeEvent.Clicked); }");
            break;
        case "System.Windows.Forms.RadioButton":
            Add("private bool __checked;");
            Add("private bool __radioTabStop;");
            Add("private void __RaiseCheckedChanged() { var handlers = CheckedChanged; if (handlers is null) return; foreach (global::System.EventHandler handler in handlers.GetInvocationList()) { if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=radio-handler-begin|name=\" + Name + \"|owner=\" + (handler.Method.DeclaringType?.FullName ?? \"<unknown>\") + \"|method=\" + handler.Method.Name); handler.Invoke(this, global::System.EventArgs.Empty); if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=radio-handler-end|name=\" + Name + \"|method=\" + handler.Method.Name); } }");
            break;
        case "System.Windows.Forms.HScrollProperties":
            Add("public HScrollProperties(ScrollableControl? container) : base(container) { __SetOrientation(false); }");
            break;
        case "System.Windows.Forms.VScrollProperties":
            Add("public VScrollProperties(ScrollableControl? container) : base(container) { __SetOrientation(true); }");
            break;
        case "System.Windows.Forms.ScrollProperties":
            Add("private ScrollableControl? __scrollOwner;");
            Add("private bool __scrollVertical;");
            Add("private bool __scrollEnabled = true;");
            Add("private bool __scrollVisible;");
            Add("private int __scrollMinimum;");
            Add("private int __scrollMaximum = 100;");
            Add("private int __scrollLargeChange = 10;");
            Add("private int __scrollSmallChange = 1;");
            Add("private int __scrollValue;");
            Add("protected ScrollProperties(ScrollableControl? container) { __scrollOwner = container; }");
            Add("internal void __SetOrientation(bool vertical) { __scrollVertical = vertical; }");
            Add("internal NativeScrollAxis __Snapshot { get { return new NativeScrollAxis(__scrollEnabled, __scrollVisible, __scrollMinimum, __scrollMaximum, LargeChange, SmallChange, __scrollValue); } }");
            Add("internal void __Refresh(NativeScrollAxis value) { __scrollEnabled = value.Enabled; __scrollVisible = value.Visible; __scrollMinimum = value.Minimum; __scrollMaximum = value.Maximum; __scrollLargeChange = value.LargeChange; __scrollSmallChange = value.SmallChange; __scrollValue = value.Value; }");
            Add("private bool __AutoOwned { get { return __scrollOwner is not null && __scrollOwner.AutoScroll; } }");
            Add("private void __Commit() { __scrollOwner?.__SetScrollAxis(__scrollVertical, __Snapshot); }");
            break;
        case "System.Windows.Forms.ScrollEventArgs":
            Add("private ScrollEventType __scrollEventType;");
            Add("private int __scrollOldValue = -1;");
            Add("private int __scrollNewValue;");
            Add("private ScrollOrientation __scrollOrientation = ScrollOrientation.HorizontalScroll;");
            break;
        case "System.Windows.Forms.ScrollableControl":
            Add("private readonly HScrollProperties __horizontalScroll;");
            Add("private readonly VScrollProperties __verticalScroll;");
            Add("private readonly DockPaddingEdges __dockPadding;");
            Add("private bool __autoScroll;");
            Add("private global::System.Drawing.Size __autoScrollMargin;");
            Add("private global::System.Drawing.Size __autoScrollMinSize;");
            Add("private bool __hScroll;");
            Add("private bool __vScroll;");
            Add("private int __scrollState = ScrollStateFullDrag;");
            Add("private ulong __lastScrollEventRevision;");
            Add("private NativeScrollState __RefreshScrollState() { var state = __native.GetScrollState(); __autoScroll = state.AutoScroll; __autoScrollMargin = state.Margin; __autoScrollMinSize = state.MinimumContentSize; __hScroll = state.Horizontal.Visible; __vScroll = state.Vertical.Visible; __horizontalScroll.__Refresh(state.Horizontal); __verticalScroll.__Refresh(state.Vertical); __scrollState = (__autoScroll ? ScrollStateAutoScrolling : 0) | (__hScroll ? ScrollStateHScrollVisible : 0) | (__vScroll ? ScrollStateVScrollVisible : 0) | (__scrollState & (ScrollStateUserHasScrolled | ScrollStateFullDrag)); return state; }");
            Add("internal override bool __NativeEvent(NativeEvent kind) { if (kind == NativeEvent.Scroll) { var state = __RefreshScrollState(); if (state.EventRevision != 0 && state.EventRevision != __lastScrollEventRevision) { __lastScrollEventRevision = state.EventRevision; __scrollState |= ScrollStateUserHasScrolled; OnScroll(new ScrollEventArgs((ScrollEventType)state.EventType, state.EventOldValue, state.EventNewValue, (ScrollOrientation)state.EventOrientation)); } return false; } return base.__NativeEvent(kind); }");
            Add("internal void __SetScrollAxis(bool vertical, NativeScrollAxis value) { if (__autoScroll) return; __native.SetScrollAxisState(vertical, value); __RefreshScrollState(); Invalidate(); }");
            Add("internal bool __ScrollByWheel(int delta) { if (delta == 0 || !__autoScroll) return false; var state = __RefreshScrollState(); if (!NativeControlBridge.InNativeCallback) { var notches = global::System.Math.Max(1, global::System.Math.Abs(delta) / 120); var movement = (delta < 0 ? 1 : -1) * notches * 48; var next = state.Vertical.Visible ? new global::System.Drawing.Point(state.Position.X, global::System.Math.Max(0, state.Position.Y + movement)) : new global::System.Drawing.Point(global::System.Math.Max(0, state.Position.X + movement), state.Position.Y); __native.SetAutoScrollPosition(next); state = __RefreshScrollState(); } return state.Horizontal.Visible || state.Vertical.Visible; }");
            Add("private void __SetAutoScroll(bool value) { if (__autoScroll == value) return; __native.SetAutoScroll(value); __RefreshScrollState(); PerformLayout(); Invalidate(); }");
            Add("private void __SetAutoScrollMargin(global::System.Drawing.Size value, bool clamp) { if (clamp) value = new global::System.Drawing.Size(global::System.Math.Max(0, value.Width), global::System.Math.Max(0, value.Height)); else if (value.Width < 0 || value.Height < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __native.SetAutoScrollMargin(value); __RefreshScrollState(); PerformLayout(); }");
            Add("private void __SetAutoScrollMinSize(global::System.Drawing.Size value) { if (value.Width < 0 || value.Height < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __native.SetAutoScrollMinSize(value); __RefreshScrollState(); PerformLayout(); }");
            Add("private void __SetAutoScrollPosition(global::System.Drawing.Point value) { __native.SetAutoScrollPosition(new global::System.Drawing.Point(global::System.Math.Max(0, value.X), global::System.Math.Max(0, value.Y))); __RefreshScrollState(); Invalidate(); }");
            break;
        case "System.Windows.Forms.ScrollableControl+DockPaddingEdges":
            Add("private int __left;");
            Add("private int __top;");
            Add("private int __right;");
            Add("private int __bottom;");
            Add("private readonly ScrollableControl? __owner;");
            Add("internal DockPaddingEdges(ScrollableControl owner) { __owner = owner; }");
            Add("private void __Set(int left, int top, int right, int bottom) { if (left < 0 || top < 0 || right < 0 || bottom < 0) throw new global::System.ArgumentOutOfRangeException(); __left = left; __top = top; __right = right; __bottom = bottom; if (__owner is not null) __owner.Padding = new Padding(left, top, right, bottom); }");
            break;
        case "System.Windows.Forms.Cursors":
            Add("private static readonly Cursor __cross = new(4u);");
            Add("private static readonly Cursor __default = new(1u);");
            Add("private static readonly Cursor __hSplit = new(6u);");
            Add("private static readonly Cursor __hand = new(3u);");
            Add("private static readonly Cursor __no = new(8u);");
            Add("private static readonly Cursor __vSplit = new(5u);");
            Add("private static readonly Cursor __sizeWE = __vSplit;");
            Add("private static readonly Cursor __wait = new(7u);");
            break;
        case "System.Windows.Forms.ListControl":
            Add("private int __selectedIndex = -1;");
            Add("private string __displayMember = string.Empty;");
            Add("private bool __formattingEnabled;");
            Add("protected virtual int __SelectionItemCount => global::System.Int32.MaxValue;");
            Add("internal void __SetSelectedIndexSilently(int value) { __selectedIndex = value; }");
            Add("protected virtual void __OnSelectedIndexChanged() { SelectedValueChanged?.Invoke(this, global::System.EventArgs.Empty); }");
            break;
        case "System.Windows.Forms.ListBox":
            Add("private readonly ObjectCollection __listItems;");
            Add("private DrawMode __listDrawMode;");
            Add("protected override int __SelectionItemCount => __listItems.Count;");
            Add("protected override void OnMouseUp(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseUp(e); if (e.Button != global::System.Windows.Forms.MouseButtons.Left || __listItems.Count == 0) return; var row = global::System.Math.Clamp(e.Y / 22, 0, __listItems.Count - 1); SelectedIndex = row; }");
            break;
        case "System.Windows.Forms.ListBox+ObjectCollection":
            Add("private readonly global::System.Collections.Generic.List<object> __items = new();");
            Add("private readonly ListBox? __owner;");
            Add("internal ObjectCollection(ListBox owner) { __owner = owner; }");
            break;
        case "System.Windows.Forms.Label":
            Add("private bool __autoEllipsis;");
            Add("private global::System.Drawing.ContentAlignment __labelTextAlign = global::System.Drawing.ContentAlignment.MiddleLeft;");
            Add("internal global::System.Drawing.ContentAlignment __ProjectionTextAlign { get { return __labelTextAlign; } }");
            break;
        case "System.Windows.Forms.LinkLabel":
            Add("private readonly LinkCollection __links = new();");
            break;
        case "System.Windows.Forms.LinkLabel+Link":
            Add("private int __linkStart;");
            Add("private int __linkLength;");
            Add("private object? __linkData;");
            break;
        case "System.Windows.Forms.LinkLabel+LinkCollection":
            Add("private readonly global::System.Collections.Generic.List<Link> __linkItems = new();");
            break;
        case "System.Windows.Forms.LinkLabelLinkClickedEventArgs":
            Add("internal LinkLabel.Link? __clickedLink;");
            break;
        case "System.Windows.Forms.ProgressBar":
            Add("private int __progressMinimum;");
            Add("private int __progressMaximum = 100;");
            Add("private int __progressValue;");
            break;
        case "System.Windows.Forms.SplitContainer":
            Add("private readonly SplitterPanel __panel1 = new();");
            Add("private readonly SplitterPanel __panel2 = new();");
            Add("private int __splitterDistance = -1;");
            Add("private int __requestedSplitterDistance = -1;");
            Add("private int __splitterWidth = 4;");
            Add("private int __panel1MinSize = 25;");
            Add("private int __panel2MinSize = 25;");
            Add("private Orientation __splitOrientation = Orientation.Vertical;");
            Add("private bool __panel1Collapsed;");
            Add("private bool __panel2Collapsed;");
            Add("private bool __splitterFixed;");
            Add("private bool __splitterDragging;");
            Add("private int __splitterDragOffset;");
            Add("private int __AxisExtent => __splitOrientation == Orientation.Vertical ? ClientSize.Width : ClientSize.Height;");
            Add("private int __ConstrainSplitter(int requested) { var available = global::System.Math.Max(0, __AxisExtent - __splitterWidth); if (__panel1Collapsed) return 0; if (__panel2Collapsed) return available; var lower = global::System.Math.Min(__panel1MinSize, available); var upper = global::System.Math.Max(0, available - __panel2MinSize); if (lower > upper) { var total = __panel1MinSize + __panel2MinSize; return total == 0 ? available / 2 : available * __panel1MinSize / total; } return global::System.Math.Clamp(requested, lower, upper); }");
            Add("private void __SetSplitterDistance(int value) { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __requestedSplitterDistance = value; var constrained = __AxisExtent <= 0 ? value : __ConstrainSplitter(value); if (__splitterDistance == constrained) return; __splitterDistance = constrained; PerformLayout(); Invalidate(); }");
            Add("private void __SetPanelCollapsed(bool first, bool value) { if ((first ? __panel1Collapsed : __panel2Collapsed) == value) return; if (value && (first ? __panel2Collapsed : __panel1Collapsed)) throw new global::System.InvalidOperationException(\"Both SplitContainer panels cannot be collapsed.\"); var panel = first ? __panel1 : __panel2; if (value && (global::System.Object.ReferenceEquals(Control.__FocusedControl, panel) || panel.__ContainsDescendant(Control.__FocusedControl!))) Focus(); if (first) __panel1Collapsed = value; else __panel2Collapsed = value; panel.Visible = !value; PerformLayout(); Invalidate(); }");
            Add("protected override void OnLayout(LayoutEventArgs levent) { var available = global::System.Math.Max(0, __AxisExtent - __splitterWidth); var requested = __requestedSplitterDistance < 0 ? available / 2 : __requestedSplitterDistance; var distance = __ConstrainSplitter(requested); __splitterDistance = distance; if (__splitOrientation == Orientation.Vertical) { __panel1.Bounds = new global::System.Drawing.Rectangle(0, 0, distance, ClientSize.Height); __panel2.Bounds = new global::System.Drawing.Rectangle(global::System.Math.Min(ClientSize.Width, distance + __splitterWidth), 0, global::System.Math.Max(0, available - distance), ClientSize.Height); } else { __panel1.Bounds = new global::System.Drawing.Rectangle(0, 0, ClientSize.Width, distance); __panel2.Bounds = new global::System.Drawing.Rectangle(0, global::System.Math.Min(ClientSize.Height, distance + __splitterWidth), ClientSize.Width, global::System.Math.Max(0, available - distance)); } base.OnLayout(levent); }");
            Add("protected override void OnMouseDown(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseDown(e); if (e.Button != MouseButtons.Left || __splitterFixed || __panel1Collapsed || __panel2Collapsed) return; var axis = __splitOrientation == Orientation.Vertical ? e.X : e.Y; if (global::System.Math.Abs(axis - __splitterDistance - __splitterWidth / 2) > global::System.Math.Max(4, __splitterWidth / 2)) return; __splitterDragging = true; __splitterDragOffset = axis - __splitterDistance; Capture = true; Focus(); }");
            Add("protected override void OnMouseMove(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseMove(e); if (!__splitterDragging) return; var axis = __splitOrientation == Orientation.Vertical ? e.X : e.Y; __SetSplitterDistance(global::System.Math.Max(0, axis - __splitterDragOffset)); }");
            Add("protected override void OnMouseUp(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseUp(e); if (e.Button != MouseButtons.Left || !__splitterDragging) return; var axis = __splitOrientation == Orientation.Vertical ? e.X : e.Y; __splitterDragging = false; Capture = false; __SetSplitterDistance(global::System.Math.Max(0, axis - __splitterDragOffset)); }");
            Add("protected override void OnPaint(global::System.Windows.Forms.PaintEventArgs e) { base.OnPaint(e); var color = Enabled ? global::System.Drawing.Color.FromArgb(124, 142, 162) : global::System.Drawing.Color.FromArgb(174, 181, 188); using var fill = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(226, 232, 238)); using var line = new global::System.Drawing.Pen(color); if (__splitOrientation == Orientation.Vertical) { e.Graphics.FillRectangle(fill, __splitterDistance, 0, __splitterWidth, Height); e.Graphics.DrawLine(line, __splitterDistance + __splitterWidth - 1, 0, __splitterDistance + __splitterWidth - 1, Height); } else { e.Graphics.FillRectangle(fill, 0, __splitterDistance, Width, __splitterWidth); e.Graphics.DrawLine(line, 0, __splitterDistance + __splitterWidth - 1, Width, __splitterDistance + __splitterWidth - 1); } }");
            Add("public int SplitterDistance { get { return global::System.Math.Max(0, __splitterDistance); } set { __SetSplitterDistance(value); } }");
            Add("public int SplitterWidth { get { return __splitterWidth; } set { if (value < 1) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __splitterWidth = value; PerformLayout(); Invalidate(); } }");
            Add("public int Panel1MinSize { get { return __panel1MinSize; } set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __panel1MinSize = value; PerformLayout(); } }");
            Add("public int Panel2MinSize { get { return __panel2MinSize; } set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __panel2MinSize = value; PerformLayout(); } }");
            Add("public bool Panel1Collapsed { get { return __panel1Collapsed; } set { __SetPanelCollapsed(true, value); } }");
            Add("public bool Panel2Collapsed { get { return __panel2Collapsed; } set { __SetPanelCollapsed(false, value); } }");
            Add("public bool IsSplitterFixed { get { return __splitterFixed; } set { __splitterFixed = value; } }");
            Add("public Orientation Orientation { get { return __splitOrientation; } set { if (__splitOrientation == value) return; __splitOrientation = value; Cursor = value == Orientation.Vertical ? Cursors.VSplit : Cursors.HSplit; __splitterDistance = -1; __requestedSplitterDistance = -1; PerformLayout(); Invalidate(); } }");
            break;
        case "System.Windows.Forms.Splitter":
            Add("private int __splitPosition;");
            Add("private bool __splitterTabStop;");
            break;
        case "System.Windows.Forms.ComboBox":
            Add("private readonly ObjectCollection __comboItems;");
            Add("private ComboBoxStyle __dropDownStyle;");
            Add("private FlatStyle __comboFlatStyle;");
            Add("private object? __selectedItem;");
            Add("private AutoCompleteMode __autoCompleteMode;");
            Add("private AutoCompleteSource __autoCompleteSource;");
            Add("private DrawMode __comboDrawMode;");
            Add("private int __dropDownHeight = 106;");
            Add("private int __dropDownWidth;");
            Add("private bool __integralHeight = true;");
            Add("private ContextMenuStrip? __comboDropDown;");
            Add("private bool __comboDropDownOpen;");
            Add("private int __comboCaret = -1;");
            Add("private int __comboAnchor = -1;");
            Add("private bool __comboSelecting;");
            Add("private int __ComboIndexAt(int x) { return __NativeFieldPositionAt(x); }");
            Add("protected override int __SelectionItemCount => __comboItems.Count;");
            Add("private void __NormalizeComboSelection() { if (__comboCaret < 0) __comboCaret = Text.Length; if (__comboAnchor < 0) __comboAnchor = __comboCaret; __comboCaret = global::System.Math.Clamp(__comboCaret, 0, Text.Length); __comboAnchor = global::System.Math.Clamp(__comboAnchor, 0, Text.Length); }");
            Add("private int __ComboSelectionStart { get { __NormalizeComboSelection(); return global::System.Math.Min(__comboCaret, __comboAnchor); } }");
            Add("private int __ComboSelectionLength { get { __NormalizeComboSelection(); return global::System.Math.Abs(__comboCaret - __comboAnchor); } }");
            Add("private void __SetComboSelection(int start, int length) { if (start < 0) throw new global::System.ArgumentOutOfRangeException(nameof(start)); if (length < 0) throw new global::System.ArgumentOutOfRangeException(nameof(length)); start = global::System.Math.Min(start, Text.Length); length = global::System.Math.Min(length, Text.Length - start); __comboAnchor = start; __comboCaret = start + length; __SetNativeFieldSelection(start, length); Invalidate(); }");
            Add("private void __MoveComboCaret(int target, bool extend) { __NormalizeComboSelection(); target = global::System.Math.Clamp(target, 0, Text.Length); if (!extend) __comboAnchor = target; __comboCaret = target; __SetNativeFieldEditState(__comboAnchor, __comboCaret); Invalidate(); }");
            Add("private void __ApplyComboEdit(NativeFieldEdit edit) { __comboAnchor = edit.Anchor; __comboCaret = edit.Caret; if (edit.Changed) __NotifyNativeFieldTextChanged(); Invalidate(); }");
            Add("private void __ReplaceComboSelection(string value, int replacementStart = -1, int replacementLength = 0) { __NormalizeComboSelection(); var start = replacementStart >= 0 ? global::System.Math.Clamp(replacementStart, 0, Text.Length) : __ComboSelectionStart; var length = replacementStart >= 0 ? global::System.Math.Clamp(replacementLength, 0, Text.Length - start) : __ComboSelectionLength; __SetSelectedIndexSilently(-1); __ApplyComboEdit(__ReplaceNativeField(start, length, value)); }");
            Add("private void __NavigateComboHistory(int direction) { __SetSelectedIndexSilently(-1); __ApplyComboEdit(__NativeFieldHistory(direction)); }");
            Add("internal void __ResetItemsSelection() { __SetSelectedIndexSilently(-1); Text = string.Empty; __comboCaret = __comboAnchor = 0; __SetNativeFieldSelection(0, 0); }");
            Add("internal override void __NativeTextInput(string text, bool composing, int replacementStart, int replacementLength) { if (__dropDownStyle != ComboBoxStyle.DropDownList && !global::System.String.IsNullOrEmpty(text)) __ReplaceComboSelection(text, replacementStart, replacementLength); }");
            Add("internal override void __NativeKeyInput(uint physicalKey, bool down, uint modifiers, bool repeat) { base.__NativeKeyInput(physicalKey, down, modifiers, repeat); if (!down || __dropDownStyle == ComboBoxStyle.DropDownList) return; __NormalizeComboSelection(); var shift = (modifiers & 1u) != 0; var control = (modifiers & 2u) != 0; if (control && physicalKey == 0x1du) { __NavigateComboHistory(-1); return; } if (control && physicalKey == 0x1cu) { __NavigateComboHistory(1); return; } if (control && physicalKey == 0x04u) { __comboAnchor = 0; __comboCaret = Text.Length; __SetNativeFieldSelection(0, Text.Length); Invalidate(); return; } if (control && physicalKey == 0x06u) { if (__ComboSelectionLength > 0) Clipboard.SetText(Text.Substring(__ComboSelectionStart, __ComboSelectionLength)); return; } if (control && physicalKey == 0x1bu) { if (__ComboSelectionLength > 0) { Clipboard.SetText(Text.Substring(__ComboSelectionStart, __ComboSelectionLength)); __ReplaceComboSelection(string.Empty); } return; } if (control && physicalKey == 0x19u) { __ReplaceComboSelection(Clipboard.GetText()); return; } if (physicalKey == 0x2au) { if (__ComboSelectionLength > 0) __ReplaceComboSelection(string.Empty); else if (__comboCaret > 0) { __comboAnchor = __PreviousNativeFieldPosition(__comboCaret); __ReplaceComboSelection(string.Empty); } } else if (physicalKey == 0x4cu) { if (__ComboSelectionLength > 0) __ReplaceComboSelection(string.Empty); else if (__comboCaret < Text.Length) { __comboAnchor = __NextNativeFieldPosition(__comboCaret); __ReplaceComboSelection(string.Empty); } } else if (physicalKey == 0x4au) __MoveComboCaret(0, shift); else if (physicalKey == 0x4du) __MoveComboCaret(Text.Length, shift); else if (physicalKey == 0x50u) { var target = !shift && __ComboSelectionLength > 0 ? __ComboSelectionStart : control ? 0 : __PreviousNativeFieldPosition(__comboCaret); __MoveComboCaret(target, shift); } else if (physicalKey == 0x4fu) { var target = !shift && __ComboSelectionLength > 0 ? __ComboSelectionStart + __ComboSelectionLength : control ? Text.Length : __NextNativeFieldPosition(__comboCaret); __MoveComboCaret(target, shift); } }");
            Add("protected override void __OnSelectedIndexChanged() { Text = SelectedIndex >= 0 && SelectedIndex < __comboItems.Count ? global::System.Convert.ToString(__comboItems[SelectedIndex], global::System.Globalization.CultureInfo.CurrentCulture) ?? string.Empty : string.Empty; __comboCaret = __comboAnchor = Text.Length; __SetNativeFieldSelection(Text.Length, 0); base.__OnSelectedIndexChanged(); SelectedIndexChanged?.Invoke(this, global::System.EventArgs.Empty); }");
            Add("private void __ComboMenuClosed() { if (!__comboDropDownOpen) return; __comboDropDownOpen = false; __NormalizeComboSelection(); __SetNativeFieldEditState(__comboAnchor, __comboCaret, __dropDownStyle != ComboBoxStyle.DropDownList && Focused); DropDownClosed?.Invoke(this, global::System.EventArgs.Empty); }");
            Add("private void __CloseItems() { if (!__comboDropDownOpen) return; __comboDropDown?.__CloseDropDown(); if (__comboDropDownOpen) __ComboMenuClosed(); }");
            Add("private void __ShowItems() { if (__dropDownStyle == ComboBoxStyle.Simple || __comboItems.Count == 0 || __comboDropDownOpen) return; if (__comboDropDown is not null) { __comboDropDown.__DropDownClosed -= __ComboMenuClosed; __comboDropDown.__CloseDropDown(); __comboDropDown.Dispose(); } var menu = new ContextMenuStrip { Name = Name + \".DropDown\" }; menu.__DropDownClosed += __ComboMenuClosed; __comboDropDown = menu; for (var index = 0; index < __comboItems.Count; ++index) { var selection = index; var text = global::System.Convert.ToString(__comboItems[index], global::System.Globalization.CultureInfo.CurrentCulture) ?? string.Empty; var item = new ToolStripMenuItem(text) { Name = \"item.\" + index.ToString(global::System.Globalization.CultureInfo.InvariantCulture), Checked = index == SelectedIndex }; item.Click += (_, _) => { SelectedIndex = selection; __CloseItems(); }; menu.Items.Add(item); } DropDown?.Invoke(this, global::System.EventArgs.Empty); __comboDropDownOpen = true; __NormalizeComboSelection(); __SetNativeFieldEditState(__comboAnchor, __comboCaret, false); menu.Show(this, new global::System.Drawing.Point(0, Height)); }");
            Add("protected override void OnMouseDown(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseDown(e); if (e.Button != global::System.Windows.Forms.MouseButtons.Left || __dropDownStyle == ComboBoxStyle.DropDownList || e.X >= global::System.Math.Max(0, Width - 22)) return; __comboSelecting = true; var target = __ComboIndexAt(e.X); __comboAnchor = __comboCaret = target; __SetNativeFieldSelection(target, 0); Invalidate(); }");
            Add("protected override void OnMouseMove(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseMove(e); if (__comboSelecting) __MoveComboCaret(__ComboIndexAt(e.X), true); }");
            Add("protected override void OnMouseUp(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseUp(e); if (e.Button != global::System.Windows.Forms.MouseButtons.Left) return; if (__dropDownStyle == ComboBoxStyle.DropDownList || e.X >= global::System.Math.Max(0, Width - 22)) { __comboSelecting = false; __ShowItems(); return; } if (__comboSelecting) __MoveComboCaret(__ComboIndexAt(e.X), true); else __MoveComboCaret(__ComboIndexAt(e.X), false); __comboSelecting = false; }");
            break;
        case "System.Windows.Forms.ComboBox+ObjectCollection":
            Add("private readonly global::System.Collections.Generic.List<object> __items = new();");
            Add("private readonly ComboBox? __owner;");
            Add("internal ObjectCollection(ComboBox owner) { __owner = owner; }");
            break;
        case "System.Windows.Forms.TextBoxBase":
            Add("private bool __textReadOnly;");
            Add("private bool __textMultiline;");
            Add("private BorderStyle __textBorderStyle = BorderStyle.Fixed3D;");
            Add("private int __textCaret = -1;");
            Add("private int __textAnchor = -1;");
            Add("private bool __textSelecting;");
            Add("private int __TextIndexAt(int x) { return __NativeFieldPositionAt(x); }");
            Add("private void __NormalizeTextSelection() { if (__textCaret < 0) __textCaret = Text.Length; if (__textAnchor < 0) __textAnchor = __textCaret; __textCaret = global::System.Math.Clamp(__textCaret, 0, Text.Length); __textAnchor = global::System.Math.Clamp(__textAnchor, 0, Text.Length); }");
            Add("private int __TextSelectionStart { get { __NormalizeTextSelection(); return global::System.Math.Min(__textCaret, __textAnchor); } }");
            Add("private int __TextSelectionLength { get { __NormalizeTextSelection(); return global::System.Math.Abs(__textCaret - __textAnchor); } }");
            Add("private void __SetTextSelection(int start, int length) { if (start < 0) throw new global::System.ArgumentOutOfRangeException(nameof(start)); if (length < 0) throw new global::System.ArgumentOutOfRangeException(nameof(length)); start = global::System.Math.Min(start, Text.Length); length = global::System.Math.Min(length, Text.Length - start); __textAnchor = start; __textCaret = start + length; __SetNativeFieldSelection(start, length); Invalidate(); }");
            Add("private void __MoveTextCaret(int target, bool extend) { __NormalizeTextSelection(); target = global::System.Math.Clamp(target, 0, Text.Length); if (!extend) __textAnchor = target; __textCaret = target; __SetNativeFieldEditState(__textAnchor, __textCaret); Invalidate(); }");
            Add("private void __ApplyTextEdit(NativeFieldEdit edit) { __textAnchor = edit.Anchor; __textCaret = edit.Caret; if (edit.Changed) __NotifyNativeFieldTextChanged(); Invalidate(); }");
            Add("private void __ReplaceTextSelection(string value, int replacementStart = -1, int replacementLength = 0) { __NormalizeTextSelection(); var start = replacementStart >= 0 ? global::System.Math.Clamp(replacementStart, 0, Text.Length) : __TextSelectionStart; var length = replacementStart >= 0 ? global::System.Math.Clamp(replacementLength, 0, Text.Length - start) : __TextSelectionLength; __ApplyTextEdit(__ReplaceNativeField(start, length, value)); }");
            Add("private void __NavigateTextHistory(int direction) { __ApplyTextEdit(__NativeFieldHistory(direction)); }");
            Add("internal override void __NativeTextInput(string text, bool composing, int replacementStart, int replacementLength) { if (!__textReadOnly && !global::System.String.IsNullOrEmpty(text)) __ReplaceTextSelection(text, replacementStart, replacementLength); }");
            Add("internal override void __NativeKeyInput(uint physicalKey, bool down, uint modifiers, bool repeat) { base.__NativeKeyInput(physicalKey, down, modifiers, repeat); if (!down) return; __NormalizeTextSelection(); var shift = (modifiers & 1u) != 0; var control = (modifiers & 2u) != 0; if (control && physicalKey == 0x06u) { if (__TextSelectionLength > 0) Clipboard.SetText(Text.Substring(__TextSelectionStart, __TextSelectionLength)); return; } if (__textReadOnly) return; if (control && physicalKey == 0x1du) { __NavigateTextHistory(-1); return; } if (control && physicalKey == 0x1cu) { __NavigateTextHistory(1); return; } if (control && physicalKey == 0x04u) { __textAnchor = 0; __textCaret = Text.Length; __SetNativeFieldSelection(0, Text.Length); Invalidate(); return; } if (control && physicalKey == 0x1bu) { if (__TextSelectionLength > 0) { Clipboard.SetText(Text.Substring(__TextSelectionStart, __TextSelectionLength)); __ReplaceTextSelection(string.Empty); } return; } if (control && physicalKey == 0x19u) { __ReplaceTextSelection(Clipboard.GetText()); return; } if (physicalKey == 0x2au) { if (__TextSelectionLength > 0) __ReplaceTextSelection(string.Empty); else if (__textCaret > 0) { __textAnchor = __PreviousNativeFieldPosition(__textCaret); __ReplaceTextSelection(string.Empty); } } else if (physicalKey == 0x4cu) { if (__TextSelectionLength > 0) __ReplaceTextSelection(string.Empty); else if (__textCaret < Text.Length) { __textAnchor = __NextNativeFieldPosition(__textCaret); __ReplaceTextSelection(string.Empty); } } else if (physicalKey == 0x4au) __MoveTextCaret(0, shift); else if (physicalKey == 0x4du) __MoveTextCaret(Text.Length, shift); else if (physicalKey == 0x50u) { var target = !shift && __TextSelectionLength > 0 ? __TextSelectionStart : control ? 0 : __PreviousNativeFieldPosition(__textCaret); __MoveTextCaret(target, shift); } else if (physicalKey == 0x4fu) { var target = !shift && __TextSelectionLength > 0 ? __TextSelectionStart + __TextSelectionLength : control ? Text.Length : __NextNativeFieldPosition(__textCaret); __MoveTextCaret(target, shift); } }");
            Add("protected override void OnMouseDown(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseDown(e); if (e.Button != global::System.Windows.Forms.MouseButtons.Left) return; __textSelecting = true; var target = __TextIndexAt(e.X); __textAnchor = __textCaret = target; __SetNativeFieldSelection(target, 0); Invalidate(); }");
            Add("protected override void OnMouseMove(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseMove(e); if (__textSelecting) __MoveTextCaret(__TextIndexAt(e.X), true); }");
            Add("protected override void OnMouseUp(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseUp(e); if (e.Button != global::System.Windows.Forms.MouseButtons.Left) return; if (__textSelecting) __MoveTextCaret(__TextIndexAt(e.X), true); else __MoveTextCaret(__TextIndexAt(e.X), false); __textSelecting = false; }");
            break;
        case "System.Windows.Forms.NumericUpDown":
            Add("private decimal __minimum;");
            Add("private decimal __maximum = 100m;");
            Add("private decimal __increment = 1m;");
            Add("private decimal __value;");
            Add("private int __decimalPlaces;");
            Add("private bool __thousandsSeparator;");
            Add("private int __numericCaret = -1;");
            Add("private int __numericAnchor = -1;");
            Add("private bool __numericSelecting;");
            Add("private int __NumericIndexAt(int x) { return __NativeFieldPositionAt(x); }");
            Add("private void __NormalizeNumericSelection() { if (__numericCaret < 0) __numericCaret = Text.Length; if (__numericAnchor < 0) __numericAnchor = __numericCaret; __numericCaret = global::System.Math.Clamp(__numericCaret, 0, Text.Length); __numericAnchor = global::System.Math.Clamp(__numericAnchor, 0, Text.Length); }");
            Add("private int __NumericSelectionStart { get { __NormalizeNumericSelection(); return global::System.Math.Min(__numericCaret, __numericAnchor); } }");
            Add("private int __NumericSelectionLength { get { __NormalizeNumericSelection(); return global::System.Math.Abs(__numericCaret - __numericAnchor); } }");
            Add("private void __MoveNumericCaret(int target, bool extend) { __NormalizeNumericSelection(); target = global::System.Math.Clamp(target, 0, Text.Length); if (!extend) __numericAnchor = target; __numericCaret = target; __SetNativeFieldEditState(__numericAnchor, __numericCaret); Invalidate(); }");
            Add("private void __UpdateNumericText() { var format = (__thousandsSeparator ? \"N\" : \"F\") + __decimalPlaces.ToString(global::System.Globalization.CultureInfo.InvariantCulture); Text = __value.ToString(format, global::System.Globalization.CultureInfo.CurrentCulture); __numericCaret = __numericAnchor = Text.Length; __SetNativeFieldSelection(Text.Length, 0); }");
            Add("private bool __IsTransientNumericText(string candidate) { var format = global::System.Globalization.CultureInfo.CurrentCulture.NumberFormat; return candidate.Length == 0 || candidate == format.NegativeSign || candidate == format.PositiveSign || candidate == format.NumberDecimalSeparator || candidate == format.NegativeSign + format.NumberDecimalSeparator || candidate == format.PositiveSign + format.NumberDecimalSeparator; }");
            Add("private bool __CanAcceptNumericText(string candidate) { return (global::System.Decimal.TryParse(candidate, global::System.Globalization.NumberStyles.Number, global::System.Globalization.CultureInfo.CurrentCulture, out var parsed) && parsed >= __minimum && parsed <= __maximum) || __IsTransientNumericText(candidate); }");
            Add("private void __ApplyNumericEdit(NativeFieldEdit edit) { __numericAnchor = edit.Anchor; __numericCaret = edit.Caret; var valueChanged = false; if (global::System.Decimal.TryParse(Text, global::System.Globalization.NumberStyles.Number, global::System.Globalization.CultureInfo.CurrentCulture, out var parsed) && parsed >= __minimum && parsed <= __maximum) { valueChanged = parsed != __value; __value = parsed; } if (edit.Changed) __NotifyNativeFieldTextChanged(); if (valueChanged) ValueChanged?.Invoke(this, global::System.EventArgs.Empty); Invalidate(); }");
            Add("private void __ReplaceNumericSelection(string value, int replacementStart = -1, int replacementLength = 0) { __NormalizeNumericSelection(); var start = replacementStart >= 0 ? global::System.Math.Clamp(replacementStart, 0, Text.Length) : __NumericSelectionStart; var length = replacementStart >= 0 ? global::System.Math.Clamp(replacementLength, 0, Text.Length - start) : __NumericSelectionLength; var candidate = Text.Remove(start, length).Insert(start, value); if (!__CanAcceptNumericText(candidate)) return; __ApplyNumericEdit(__ReplaceNativeField(start, length, value)); }");
            Add("private void __NavigateNumericHistory(int direction) { __ApplyNumericEdit(__NativeFieldHistory(direction)); }");
            Add("private void __CommitNumericText() { if (!global::System.Decimal.TryParse(Text, global::System.Globalization.NumberStyles.Number, global::System.Globalization.CultureInfo.CurrentCulture, out var parsed)) { __UpdateNumericText(); return; } parsed = global::System.Math.Clamp(parsed, __minimum, __maximum); if (parsed != __value) { __value = parsed; ValueChanged?.Invoke(this, global::System.EventArgs.Empty); } __UpdateNumericText(); Invalidate(); }");
            Add("private void __Spin(bool increase) { var candidate = __value + (increase ? __increment : -__increment); Value = candidate < __minimum ? __minimum : candidate > __maximum ? __maximum : candidate; }");
            Add("internal override void __NativeTextInput(string text, bool composing, int replacementStart, int replacementLength) { if (!global::System.String.IsNullOrEmpty(text)) __ReplaceNumericSelection(text, replacementStart, replacementLength); }");
            Add("internal override void __NativeKeyInput(uint physicalKey, bool down, uint modifiers, bool repeat) { base.__NativeKeyInput(physicalKey, down, modifiers, repeat); if (!down) return; __NormalizeNumericSelection(); var shift = (modifiers & 1u) != 0; var control = (modifiers & 2u) != 0; if (control && physicalKey == 0x1du) { __NavigateNumericHistory(-1); return; } if (control && physicalKey == 0x1cu) { __NavigateNumericHistory(1); return; } if (control && physicalKey == 0x04u) { __numericAnchor = 0; __numericCaret = Text.Length; __SetNativeFieldSelection(0, Text.Length); Invalidate(); return; } if (control && physicalKey == 0x06u) { if (__NumericSelectionLength > 0) Clipboard.SetText(Text.Substring(__NumericSelectionStart, __NumericSelectionLength)); return; } if (control && physicalKey == 0x1bu) { if (__NumericSelectionLength > 0) { Clipboard.SetText(Text.Substring(__NumericSelectionStart, __NumericSelectionLength)); __ReplaceNumericSelection(string.Empty); } return; } if (control && physicalKey == 0x19u) { __ReplaceNumericSelection(Clipboard.GetText()); return; } if (physicalKey == 0x28u) { __CommitNumericText(); return; } if (physicalKey == 0x2au) { if (__NumericSelectionLength > 0) __ReplaceNumericSelection(string.Empty); else if (__numericCaret > 0) { __numericAnchor = __PreviousNativeFieldPosition(__numericCaret); __ReplaceNumericSelection(string.Empty); } } else if (physicalKey == 0x4cu) { if (__NumericSelectionLength > 0) __ReplaceNumericSelection(string.Empty); else if (__numericCaret < Text.Length) { __numericAnchor = __NextNativeFieldPosition(__numericCaret); __ReplaceNumericSelection(string.Empty); } } else if (physicalKey == 0x4au) __MoveNumericCaret(0, shift); else if (physicalKey == 0x4du) __MoveNumericCaret(Text.Length, shift); else if (physicalKey == 0x50u) { var target = !shift && __NumericSelectionLength > 0 ? __NumericSelectionStart : control ? 0 : __PreviousNativeFieldPosition(__numericCaret); __MoveNumericCaret(target, shift); } else if (physicalKey == 0x4fu) { var target = !shift && __NumericSelectionLength > 0 ? __NumericSelectionStart + __NumericSelectionLength : control ? Text.Length : __NextNativeFieldPosition(__numericCaret); __MoveNumericCaret(target, shift); } }");
            Add("protected override void OnLostFocus(global::System.EventArgs e) { __CommitNumericText(); base.OnLostFocus(e); }");
            Add("protected override void OnMouseDown(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseDown(e); if (e.Button != global::System.Windows.Forms.MouseButtons.Left || e.X >= global::System.Math.Max(0, Width - 18)) return; __numericSelecting = true; var target = __NumericIndexAt(e.X); __numericAnchor = __numericCaret = target; __SetNativeFieldSelection(target, 0); Invalidate(); }");
            Add("protected override void OnMouseMove(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseMove(e); if (__numericSelecting) __MoveNumericCaret(__NumericIndexAt(e.X), true); }");
            Add("protected override void OnMouseUp(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseUp(e); if (e.Button != global::System.Windows.Forms.MouseButtons.Left) return; if (e.X >= global::System.Math.Max(0, Width - 18)) { __numericSelecting = false; __Spin(e.Y < Height / 2); return; } if (__numericSelecting) __MoveNumericCaret(__NumericIndexAt(e.X), true); else __MoveNumericCaret(__NumericIndexAt(e.X), false); __numericSelecting = false; }");
            Add("protected override void OnMouseWheel(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseWheel(e); if (e.Delta != 0) __Spin(e.Delta > 0); }");
            break;
        case "System.Windows.Forms.UpDownBase":
            Add("private int __initializationDepth;");
            Add("private BorderStyle __upDownBorderStyle = BorderStyle.Fixed3D;");
            Add("private HorizontalAlignment __upDownTextAlign;");
            Add("public void BeginInit() { ++__initializationDepth; }");
            Add("public void EndInit() { if (__initializationDepth == 0) return; if (--__initializationDepth == 0) PerformLayout(); }");
            break;
        case "System.Windows.Forms.PictureBox":
            Add("private int __initializationDepth;");
            Add("private BorderStyle __pictureBorderStyle;");
            Add("private global::System.Drawing.Image? __pictureImage;");
            Add("private global::System.Drawing.Image? __errorImage;");
            Add("private global::System.Drawing.Image? __initialImage;");
            Add("private PictureBoxSizeMode __pictureSizeMode;");
            Add("internal global::System.Drawing.Image? __ProjectionPictureImage { get { return __pictureImage; } }");
            Add("internal PictureBoxSizeMode __ProjectionPictureSizeMode { get { return __pictureSizeMode; } }");
            Add("private global::System.Drawing.Rectangle __PictureZoom(global::System.Drawing.Image image) { if (image.Width <= 0 || image.Height <= 0 || Width <= 0 || Height <= 0) return global::System.Drawing.Rectangle.Empty; var scale = global::System.Math.Min(Width / (double)image.Width, Height / (double)image.Height); var width = global::System.Math.Max(1, (int)global::System.Math.Round(image.Width * scale)); var height = global::System.Math.Max(1, (int)global::System.Math.Round(image.Height * scale)); return new global::System.Drawing.Rectangle((Width - width) / 2, (Height - height) / 2, width, height); }");
            Add("private global::System.Drawing.Rectangle __PictureDestination(global::System.Drawing.Image image) { return __pictureSizeMode switch { PictureBoxSizeMode.StretchImage => ClientRectangle, PictureBoxSizeMode.CenterImage => new global::System.Drawing.Rectangle((Width - image.Width) / 2, (Height - image.Height) / 2, image.Width, image.Height), PictureBoxSizeMode.Zoom => __PictureZoom(image), _ => new global::System.Drawing.Rectangle(0, 0, image.Width, image.Height) }; }");
            Add("protected override void OnPaint(global::System.Windows.Forms.PaintEventArgs e) { if (__pictureImage is global::System.Drawing.Image image) { var destination = __PictureDestination(image); if (destination.Width > 0 && destination.Height > 0) e.Graphics.DrawImage(image, destination); } base.OnPaint(e); }");
            Add("public void BeginInit() { ++__initializationDepth; }");
            Add("public void EndInit() { if (__initializationDepth == 0) return; if (--__initializationDepth == 0) { PerformLayout(); Parent?.PerformLayout(); Invalidate(); } }");
            break;
        case "System.Windows.Forms.Panel":
            Add("private AutoSizeMode __panelAutoSizeMode;");
            Add("private BorderStyle __panelBorderStyle;");
            break;
        case "System.Windows.Forms.FlatButtonAppearance":
            Add("private int __flatBorderSize = 1;");
            Add("private ButtonBase? __owner;");
            Add("internal int __BorderSize { get { return __flatBorderSize; } }");
            Add("internal void __Attach(ButtonBase owner) { __owner = owner; }");
            break;
        case "System.Windows.Forms.GroupBox":
            Add("private FlatStyle __groupFlatStyle = FlatStyle.Standard;");
            Add("private bool __groupTabStop;");
            break;
        case "System.Windows.Forms.KeyPressEventArgs":
            Add("private bool __keyPressHandled;");
            break;
        case "System.Windows.Forms.HandledMouseEventArgs":
            Add("private bool __mouseHandled;");
            break;
        case "System.Windows.Forms.StatusStrip":
            Add("private bool __statusTabStop;");
            break;
        case "System.Windows.Forms.TextBox":
            Add("private HorizontalAlignment __textBoxAlignment;");
            break;
        case "System.Windows.Forms.UserControl":
            Add("private AutoSizeMode __userControlAutoSizeMode;");
            break;
        case "System.Windows.Forms.DataGridView":
            Add("private int __initializationDepth;");
            Add("private readonly DataGridViewColumnCollection __gridColumns;");
            Add("private readonly DataGridViewRowCollection __gridRows;");
            Add("private readonly DataGridViewSelectedCellCollection __selectedCells = new();");
            Add("private readonly DataGridViewSelectedRowCollection __selectedRows = new();");
            Add("private readonly DataGridViewCellStyle __alternatingRowsDefaultCellStyle = new();");
            Add("private readonly DataGridViewCellStyle __columnHeadersDefaultCellStyle = new();");
            Add("private readonly DataGridViewCellStyle __defaultCellStyle = new();");
            Add("private readonly DataGridViewRow __rowTemplate = new();");
            Add("private readonly global::System.Collections.Generic.Dictionary<(int Column, int Row), DataGridViewCell> __cells = new();");
            Add("private object? __gridDataSource;");
            Add("private bool __allowUserToAddRows = true, __allowUserToDeleteRows = true, __allowUserToResizeColumns = true, __allowUserToResizeRows = true, __autoGenerateColumns = true;");
            Add("private global::System.Drawing.Color __gridBackgroundColor, __gridColor;");
            Add("private BorderStyle __gridBorderStyle;");
            Add("private DataGridViewHeaderBorderStyle __columnHeaderBorderStyle, __rowHeaderBorderStyle;");
            Add("private int __columnHeadersHeight = 23, __firstDisplayedRow;");
            Add("private DataGridViewColumnHeadersHeightSizeMode __columnHeadersHeightSizeMode;");
            Add("private bool __enableHeadersVisualStyles = true, __multiSelect = true, __gridReadOnly, __rowHeadersVisible = true;");
            Add("private ScrollBars __gridScrollBars = ScrollBars.Both;");
            Add("private DataGridViewSelectionMode __gridSelectionMode;");
            Add("private bool __showCellErrors = true, __showCellToolTips = true, __showEditingIcon = true, __showRowErrors = true;");
            Add("internal int __DataRowCount { get { if (__gridDataSource is BindingSource source) return source.List.Count; if (__gridDataSource is global::System.Collections.IList list) return list.Count; return 0; } }");
            Add("private DataGridViewCell __Cell(int columnIndex, int rowIndex) { if (columnIndex < 0 || rowIndex < 0) throw new global::System.ArgumentOutOfRangeException(); var key = (columnIndex, rowIndex); if (!__cells.TryGetValue(key, out var cell)) { cell = new DataGridViewTextBoxCell { __dataGridView = this, __rowIndex = rowIndex }; __cells.Add(key, cell); } return cell; }");
            Add("public void BeginInit() { ++__initializationDepth; }");
            Add("public void EndInit() { if (__initializationDepth == 0) return; if (--__initializationDepth == 0) PerformLayout(); }");
            break;
        case "System.Windows.Forms.PropertyGrid":
            Add("private object[] __propertyGridSelectedObjects = global::System.Array.Empty<object>();");
            Add("private NativeControlBridge.PropertyObjectAdapter[] __propertyGridAdapters = global::System.Array.Empty<NativeControlBridge.PropertyObjectAdapter>();");
            Add("private PropertySort __propertyGridSort = PropertySort.CategorizedAlphabetical;");
            Add("private void __SetPropertyGridSelection(object[]? values) { var next = values is null ? global::System.Array.Empty<object>() : (object[])values.Clone(); var identities = new global::System.Collections.Generic.HashSet<object>(global::System.Collections.Generic.ReferenceEqualityComparer.Instance); foreach (var value in next) { if (value is null) throw new global::System.ArgumentNullException(nameof(values), \"PropertyGrid.SelectedObjects cannot contain null.\"); if (!identities.Add(value)) throw new global::System.ArgumentException(\"PropertyGrid.SelectedObjects requires unique object identities.\", nameof(values)); } var replacement = __native.SetPropertyGridSelectedObjects(next, this); var retired = __propertyGridAdapters; __propertyGridAdapters = replacement; __propertyGridSelectedObjects = next; foreach (var adapter in retired) adapter.Dispose(); SelectedObjectsChanged?.Invoke(this, global::System.EventArgs.Empty); }");
            Add("internal bool __TrySetPropertyText(string name, string value) => __native.TrySetPropertyGridText(name, value);");
            Add("internal bool __ResetProperty(string name) => __native.ResetPropertyGridProperty(name);");
            Add("internal bool __EditProperty(string name) => __native.ActivatePropertyGridEditor(name);");
            Add("protected override void Dispose(bool disposing) { if (disposing) { try { __native.SetPropertyGridSelectedObjects(global::System.Array.Empty<object>(), this); } finally { foreach (var adapter in __propertyGridAdapters) adapter.Dispose(); __propertyGridAdapters = global::System.Array.Empty<NativeControlBridge.PropertyObjectAdapter>(); __propertyGridSelectedObjects = global::System.Array.Empty<object>(); } } base.Dispose(disposing); }");
            break;
        case "System.Windows.Forms.DataGridViewElement":
            Add("internal DataGridView? __dataGridView;");
            break;
        case "System.Windows.Forms.DataGridViewBand":
            Add("private DataGridViewCellStyle __bandDefaultCellStyle = new();");
            Add("private bool __bandReadOnly;");
            Add("private bool __bandSelected;");
            Add("private DataGridViewTriState __bandResizable = DataGridViewTriState.NotSet;");
            break;
        case "System.Windows.Forms.DataGridViewCell":
            Add("private readonly DataGridViewCellStyle __cellStyle = new();");
            Add("internal object? __cellValue;");
            Add("internal int __rowIndex = -1;");
            break;
        case "System.Windows.Forms.DataGridViewCellStyle":
            Add("private DataGridViewContentAlignment __alignment;");
            Add("private global::System.Drawing.Color __styleBackColor;");
            Add("private global::System.Drawing.Color __styleForeColor;");
            Add("private global::System.Drawing.Color __selectionBackColor;");
            Add("private global::System.Drawing.Font? __styleFont;");
            Add("private string __format = string.Empty;");
            break;
        case "System.Windows.Forms.DataGridViewColumn":
            Add("private DataGridViewCell? __cellTemplate;");
            Add("private string __dataPropertyName = string.Empty;");
            Add("private string __headerText = string.Empty;");
            Add("private string __columnName = string.Empty;");
            Add("private int __columnWidth = 100;");
            Add("private DataGridViewAutoSizeColumnMode __columnAutoSizeMode;");
            Add("private float __fillWeight = 100f;");
            break;
        case "System.Windows.Forms.DataGridViewCellFormattingEventArgs":
            Add("internal int __formattingColumnIndex = -1;");
            Add("private bool __formattingApplied;");
            break;
        case "System.Windows.Forms.DataGridViewRow":
            Add("private int __rowHeight = 22;");
            break;
        case "System.Windows.Forms.DataGridViewColumnCollection":
            Add("private readonly DataGridView? __owner;");
            Add("private readonly global::System.Collections.Generic.List<DataGridViewColumn> __items = new();");
            Add("internal DataGridViewColumnCollection(DataGridView owner) { __owner = owner; }");
            Add("protected override global::System.Collections.IList __Collection { get { return __items; } }");
            Add("public override int Count { get { return __items.Count; } }");
            break;
        case "System.Windows.Forms.DataGridViewRowCollection":
            Add("private readonly DataGridView? __owner;");
            Add("private readonly global::System.Collections.Generic.List<DataGridViewRow> __rows = new();");
            Add("internal DataGridViewRowCollection(DataGridView owner) { __owner = owner; }");
            Add("private void __EnsureRows() { var count = __owner?.__DataRowCount ?? 0; while (__rows.Count < count) { var row = new DataGridViewRow(); row.__dataGridView = __owner; __rows.Add(row); } if (__rows.Count > count) __rows.RemoveRange(count, __rows.Count - count); }");
            break;
        case "System.Windows.Forms.DataGridViewSelectedCellCollection":
        case "System.Windows.Forms.DataGridViewSelectedRowCollection":
            Add("protected override global::System.Collections.IList __Collection { get { return global::System.Array.Empty<object>(); } }");
            break;
        case "System.Windows.Forms.BindingSource":
            Add("private int __initializationDepth;");
            Add("private object? __bindingDataSource;");
            Add("private global::System.Collections.IList __bindingList = new global::System.Collections.ArrayList();");
            Add("private int __position = -1;");
            Add("private void __SetDataSource(object? value) { __bindingDataSource = value; __bindingList = value switch { BindingSource source => source.List, global::System.Collections.IList list => list, global::System.Collections.IEnumerable sequence when value is not string => new global::System.Collections.ArrayList(global::System.Linq.Enumerable.ToArray(global::System.Linq.Enumerable.Cast<object>(sequence))), _ => new global::System.Collections.ArrayList() }; __position = __bindingList.Count == 0 ? -1 : global::System.Math.Clamp(__position < 0 ? 0 : __position, 0, __bindingList.Count - 1); }");
            Add("public void BeginInit() { ++__initializationDepth; }");
            Add("public void EndInit() { if (__initializationDepth != 0) --__initializationDepth; }");
            break;
        case "System.Windows.Forms.Timer":
            Add("private readonly object __timerGate = new();");
            Add("private readonly int __ownerThreadId = global::System.Environment.CurrentManagedThreadId;");
            Add("private global::System.Threading.Timer? __timer;");
            Add("private int __interval = 100;");
            Add("private volatile bool __timerEnabled;");
            Add("private int __tickPending;");
            Add("private void __RaiseTick() { var handlers = Tick; if (handlers is null) return; if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_TIMERS\") != \"1\") { handlers.Invoke(this, global::System.EventArgs.Empty); return; } foreach (global::System.EventHandler handler in handlers.GetInvocationList()) { var method = handler.Method; var owner = method.DeclaringType?.FullName ?? \"<unknown>\"; var started = global::System.Diagnostics.Stopwatch.GetTimestamp(); global::System.Console.Error.WriteLine(\"facade-timer=begin|owner:\" + owner + \"|method:\" + method.Name + \"|interval:\" + __interval); try { handler.Invoke(this, global::System.EventArgs.Empty); } finally { global::System.Console.Error.WriteLine(\"facade-timer=end|owner:\" + owner + \"|method:\" + method.Name + \"|elapsed-ms:\" + global::System.Diagnostics.Stopwatch.GetElapsedTime(started).TotalMilliseconds.ToString(\"F3\", global::System.Globalization.CultureInfo.InvariantCulture)); } } }");
            Add("private void __Schedule() { lock (__timerGate) { __timer?.Dispose(); global::System.Threading.Interlocked.Exchange(ref __tickPending, 0); __timer = __timerEnabled ? new global::System.Threading.Timer(_ => { if (!__timerEnabled || global::System.Threading.Interlocked.Exchange(ref __tickPending, 1) != 0) return; if (!Application.__Post(__ownerThreadId, () => { global::System.Threading.Interlocked.Exchange(ref __tickPending, 0); if (__timerEnabled) __RaiseTick(); })) global::System.Threading.Interlocked.Exchange(ref __tickPending, 0); }, null, __interval, __interval) : null; } }");
            Add("protected override void Dispose(bool disposing) { if (disposing) { __timerEnabled = false; global::System.Threading.Interlocked.Exchange(ref __tickPending, 0); lock (__timerGate) { __timer?.Dispose(); __timer = null; } } base.Dispose(disposing); }");
            break;
        case "System.Windows.Forms.ToolStrip":
            Add("private readonly ToolStripItemCollection __toolItems;");
            Add("private Padding __gripMargin;");
            Add("private ToolStripRenderMode __renderMode;");
            Add("private ToolStripRenderer? __renderer;");
            Add("private bool __toolTraceDone;");
            Add("private ToolStripItem? __hotItem;");
            Add("private readonly global::System.Collections.Generic.List<Label> __dropDownLabels = new();");
            Add("private readonly int __menuOwnerThreadId = global::System.Environment.CurrentManagedThreadId;");
            Add("private readonly object __menuHoverGate = new();");
            Add("private global::System.Threading.Timer? __menuHoverTimer;");
            Add("private int __menuHoverGeneration;");
            Add("internal void __RefreshItems() { if (__HasRaisedLoad) __RenderManagedPaint(); }");
            Add("internal void __TraceToolItems() { if (__toolTraceDone || global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_TOOLSTRIP\") != \"1\") return; __toolTraceDone = true; var rows = new global::System.Collections.Generic.List<string>(); foreach (ToolStripItem item in __toolItems) { var image = item.__ProjectionImage; rows.Add(item.GetType().Name + \":text=\" + item.__ProjectionText + \",tip=\" + item.__ProjectionToolTip + \",size=\" + item.__ProjectionSize.Width + \"x\" + item.__ProjectionSize.Height + \",image=\" + (image is null ? \"none\" : image.Width + \"x\" + image.Height)); } global::System.Console.Error.WriteLine(\"facade-toolstrip=name:\" + Name + \"|size:\" + Width + \"x\" + Height + \"|items:\" + global::System.String.Join(\";\", rows)); }");
            Add("private static string[] __MenuTextLines(ToolStripItem item) { return item.__ProjectionText.Replace(\"\\r\\n\", \"\\n\", global::System.StringComparison.Ordinal).Replace('\\r', '\\n').Split('\\n'); }");
            Add("private static int __ItemWidth(ToolStripItem item) { var size = item.__ProjectionSize; if (size.Width > 0) return size.Width; if (item.__ProjectionImage is not null) return 34; var width = 24; foreach (var line in __MenuTextLines(item)) width = global::System.Math.Max(width, line.Length * 7 + 14); return width; }");
            Add("private int __DropDownRowHeight(ToolStripItem item) { if (item is ToolStripSeparator) return 9; if (item.__ProjectionSize.Height > 0) return global::System.Math.Max(24, item.__ProjectionSize.Height); return global::System.Math.Max(24, __MenuTextLines(item).Length * 15 + 8); }");
            Add("private int __DropDownItemTop(ToolStripItem sought) { var y = 4; foreach (ToolStripItem item in __toolItems) { if (!item.__ProjectionVisible) continue; if (global::System.Object.ReferenceEquals(item, sought)) return y; y += __DropDownRowHeight(item); } return 4; }");
            Add("private global::System.Collections.Generic.List<ToolStripItem> __SelectableItems() { var items = new global::System.Collections.Generic.List<ToolStripItem>(); foreach (ToolStripItem item in __toolItems) if (item.__ProjectionVisible && item.__ProjectionEnabled && item is not ToolStripSeparator) items.Add(item); return items; }");
            Add("private void __CancelHoverAction() { global::System.Threading.Interlocked.Increment(ref __menuHoverGeneration); lock (__menuHoverGate) { __menuHoverTimer?.Dispose(); __menuHoverTimer = null; } }");
            Add("private void __SetHotItem(ToolStripItem? item, bool scheduleHover) { if (global::System.Object.ReferenceEquals(item, __hotItem)) return; __hotItem = item; __CancelHoverAction(); __RenderManagedPaint(); if (!scheduleHover || this is not ToolStripDropDown dropDown || !Visible) return; var generation = __menuHoverGeneration; lock (__menuHoverGate) __menuHoverTimer = new global::System.Threading.Timer(_ => { _ = Application.__Post(__menuOwnerThreadId, () => { if (generation != __menuHoverGeneration || !Visible || !global::System.Object.ReferenceEquals(item, __hotItem)) return; if (item is ToolStripDropDownItem branch && branch.DropDownItems.Count > 0) __OpenBranch(branch, false); else dropDown.__CloseChildDropDown(); }); }, null, 400, global::System.Threading.Timeout.Infinite); }");
            Add("private void __MoveHot(int delta, bool edge) { var items = __SelectableItems(); if (items.Count == 0) return; var index = __hotItem is null ? -1 : items.IndexOf(__hotItem); index = edge ? (delta < 0 ? 0 : items.Count - 1) : index < 0 ? (delta < 0 ? items.Count - 1 : 0) : (index + delta + items.Count) % items.Count; __SetHotItem(items[index], false); }");
            Add("private void __OpenBranch(ToolStripDropDownItem branch, bool focusChild) { if (branch.DropDownItems.Count == 0) return; var location = PointToScreen(new global::System.Drawing.Point(global::System.Math.Max(0, Width - 2), __DropDownItemTop(branch))); if (this is ToolStripDropDown parentDropDown) parentDropDown.__ShowChildDropDown(branch.DropDown, location, focusChild); else { branch.DropDown.Show(location); if (focusChild) branch.DropDown.__FocusFirstItem(); } }");
            Add("private void __ActivateHotItem() { var item = __hotItem; if (item is null || !item.__ProjectionEnabled) return; if (item is ToolStripDropDownItem branch && branch.DropDownItems.Count > 0) { __OpenBranch(branch, true); return; } item.__PerformClick(); if (this is ToolStripDropDown dropDown && Visible) dropDown.__CloseMenuChain(); }");
            Add("internal void __FocusFirstItem() { var items = __SelectableItems(); if (items.Count != 0) __SetHotItem(items[0], false); Focus(); }");
            Add("internal void __PrepareDropDown(global::System.Drawing.Point screenLocation) { var width = 136; var height = 4; foreach (ToolStripItem item in __toolItems) if (item.__ProjectionVisible) { width = global::System.Math.Max(width, __ItemWidth(item) + 54); height += __DropDownRowHeight(item); } width = global::System.Math.Min(420, width); Bounds = new global::System.Drawing.Rectangle(screenLocation.X, screenLocation.Y, width, global::System.Math.Max(20, height + 4)); BackColor = global::System.Drawing.Color.FromArgb(247, 249, 252); ForeColor = global::System.Drawing.Color.FromArgb(31, 37, 44); foreach (var label in __dropDownLabels) { Controls.Remove(label); label.Dispose(); } __dropDownLabels.Clear(); }");
            Add("private void __PaintDropDownItems(global::System.Drawing.Graphics graphics) { using var background = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(247, 249, 252)); using var rail = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(230, 235, 241)); using var hot = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(218, 231, 247)); using var ink = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(31, 37, 44)); using var disabledInk = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(145, 151, 158)); using var font = new global::System.Drawing.Font(\"Portsmouth Rapids\", 12f, global::System.Drawing.FontStyle.Regular, global::System.Drawing.GraphicsUnit.Pixel, 1); using var border = new global::System.Drawing.Pen(global::System.Drawing.Color.FromArgb(126, 139, 153)); using var separator = new global::System.Drawing.Pen(global::System.Drawing.Color.FromArgb(188, 197, 207)); graphics.FillRectangle(background, ClientRectangle); graphics.FillRectangle(rail, 1, 1, 31, global::System.Math.Max(0, Height - 2)); var y = 4; foreach (ToolStripItem item in __toolItems) { if (!item.__ProjectionVisible) continue; var rowHeight = __DropDownRowHeight(item); if (item is ToolStripSeparator) { graphics.DrawLine(separator, 34, y + rowHeight / 2, global::System.Math.Max(35, Width - 5), y + rowHeight / 2); y += rowHeight; continue; } var row = new global::System.Drawing.Rectangle(3, y, global::System.Math.Max(0, Width - 6), rowHeight); if (global::System.Object.ReferenceEquals(item, __hotItem) && item.__ProjectionEnabled) { graphics.FillRectangle(hot, row); graphics.DrawRectangle(border, row.X, row.Y, global::System.Math.Max(0, row.Width - 1), global::System.Math.Max(0, row.Height - 1)); } if (item is ToolStripMenuItem menu && menu.Checked) { graphics.DrawLine(border, 10, y + rowHeight / 2, 14, y + rowHeight / 2 + 4); graphics.DrawLine(border, 14, y + rowHeight / 2 + 4, 22, y + rowHeight / 2 - 5); } var image = item.__ProjectionImage; if (image is not null) graphics.DrawImage(image, new global::System.Drawing.Rectangle(7, y + global::System.Math.Max(2, (rowHeight - 20) / 2), 20, 20)); var lines = __MenuTextLines(item); var textTop = y + global::System.Math.Max(4f, (rowHeight - lines.Length * 15f) / 2f); for (var lineIndex = 0; lineIndex < lines.Length; ++lineIndex) if (lines[lineIndex].Length != 0) graphics.DrawString(lines[lineIndex], font, item.__ProjectionEnabled ? ink : disabledInk, 39f, textTop + lineIndex * 15f); if (item is ToolStripDropDownItem child && child.DropDownItems.Count > 0) { var arrowX = Width - 14; var mid = y + rowHeight / 2; graphics.DrawLine(border, arrowX, mid - 4, arrowX + 4, mid); graphics.DrawLine(border, arrowX + 4, mid, arrowX, mid + 4); } y += rowHeight; } graphics.DrawRectangle(border, 0, 0, global::System.Math.Max(0, Width - 1), global::System.Math.Max(0, Height - 1)); }");
            Add("private void __PaintItems(global::System.Drawing.Graphics graphics) { if (this is ToolStripDropDown) { __PaintDropDownItems(graphics); return; } graphics.SmoothingMode = global::System.Drawing.Drawing2D.SmoothingMode.AntiAlias; using var background = new global::System.Drawing.SolidBrush(BackColor); graphics.FillRectangle(background, ClientRectangle); using var border = new global::System.Drawing.Pen(global::System.Drawing.Color.FromArgb(110, ForeColor)); using var face = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(24, ForeColor)); using var ink = new global::System.Drawing.SolidBrush(ForeColor); using var font = new global::System.Drawing.Font(\"Portsmouth Rapids\", 10f, global::System.Drawing.FontStyle.Regular, global::System.Drawing.GraphicsUnit.Pixel, 1); var x = global::System.Math.Max(2, __gripMargin.Left); foreach (ToolStripItem item in __toolItems) { if (!item.__ProjectionVisible) continue; var margin = item.__ProjectionMargin; x += margin.Left; var width = __ItemWidth(item); var height = item.__ProjectionSize.Height > 0 ? global::System.Math.Min(Height - 2, item.__ProjectionSize.Height) : global::System.Math.Max(1, Height - 6); var top = global::System.Math.Max(1, (Height - height) / 2); if (item is ToolStripSeparator) { var separatorX = x + width / 2; graphics.DrawLine(border, separatorX, top + 4, separatorX, top + height - 4); } else { var bounds = new global::System.Drawing.Rectangle(x, top, width, height); graphics.FillRectangle(face, bounds); graphics.DrawRectangle(border, bounds.X, bounds.Y, global::System.Math.Max(0, bounds.Width - 1), global::System.Math.Max(0, bounds.Height - 1)); var image = item.__ProjectionImage; var text = item.__ProjectionText; if (image is not null) { var side = global::System.Math.Min(24, global::System.Math.Min(bounds.Width - 6, bounds.Height - 6)); if (side > 0) graphics.DrawImage(image, new global::System.Drawing.Rectangle(bounds.X + (bounds.Width - side) / 2, bounds.Y + (bounds.Height - side) / 2, side, side)); } else if (!global::System.String.IsNullOrEmpty(text)) { var measured = graphics.MeasureString(text, font); graphics.DrawString(text, font, ink, bounds.X + global::System.Math.Max(4f, (bounds.Width - measured.Width) / 2f), bounds.Y + global::System.Math.Max(2f, (bounds.Height - measured.Height) / 2f)); } } x += width + margin.Right; } if (!__toolTraceDone && global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_TOOLSTRIP\") == \"1\") { __toolTraceDone = true; var rows = new global::System.Collections.Generic.List<string>(); foreach (ToolStripItem item in __toolItems) { var image = item.__ProjectionImage; rows.Add(item.GetType().Name + \":text=\" + item.__ProjectionText + \",tip=\" + item.__ProjectionToolTip + \",size=\" + item.__ProjectionSize.Width + \"x\" + item.__ProjectionSize.Height + \",image=\" + (image is null ? \"none\" : image.Width + \"x\" + image.Height)); } global::System.Console.Error.WriteLine(\"facade-toolstrip=name:\" + Name + \"|size:\" + Width + \"x\" + Height + \"|items:\" + global::System.String.Join(\";\", rows)); } }");
            Add("private ToolStripItem? __ItemAt(int pointX, int pointY) { if (this is ToolStripDropDown) { var y = 4; foreach (ToolStripItem item in __toolItems) { if (!item.__ProjectionVisible) continue; var rowHeight = __DropDownRowHeight(item); if (item is not ToolStripSeparator && new global::System.Drawing.Rectangle(3, y, global::System.Math.Max(0, Width - 6), rowHeight).Contains(pointX, pointY)) return item; y += rowHeight; } return null; } var x = global::System.Math.Max(2, __gripMargin.Left); foreach (ToolStripItem item in __toolItems) { if (!item.__ProjectionVisible) continue; var margin = item.__ProjectionMargin; x += margin.Left; var width = __ItemWidth(item); var height = item.__ProjectionSize.Height > 0 ? global::System.Math.Min(Height - 2, item.__ProjectionSize.Height) : global::System.Math.Max(1, Height - 6); var top = global::System.Math.Max(1, (Height - height) / 2); if (item is not ToolStripSeparator && new global::System.Drawing.Rectangle(x, top, width, height).Contains(pointX, pointY)) return item; x += width + margin.Right; } return null; }");
            Add("protected override void OnPaint(PaintEventArgs e) { __PaintItems(e.Graphics); base.OnPaint(e); }");
            Add("protected override void OnMouseMove(MouseEventArgs e) { base.OnMouseMove(e); __SetHotItem(__ItemAt(e.X, e.Y), true); }");
            Add("protected override void OnMouseLeave(global::System.EventArgs e) { base.OnMouseLeave(e); __CancelHoverAction(); if (this is not ToolStripDropDown dropDown || !dropDown.__HasVisibleChild) __SetHotItem(null, false); }");
            Add("protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); if (e.Button != MouseButtons.Left) return; var item = __ItemAt(e.X, e.Y); if (item is null || !item.__ProjectionEnabled) return; __SetHotItem(item, false); __ActivateHotItem(); }");
            Add("protected override void OnKeyDown(KeyEventArgs e) { var handled = false; if (this is ToolStripDropDown dropDown && Visible) { switch (e.KeyCode) { case Keys.Down: __MoveHot(1, false); handled = true; break; case Keys.Up: __MoveHot(-1, false); handled = true; break; case Keys.Home: __MoveHot(-1, true); handled = true; break; case Keys.End: __MoveHot(1, true); handled = true; break; case Keys.Right: if (__hotItem is ToolStripDropDownItem branch && branch.DropDownItems.Count > 0) __OpenBranch(branch, true); handled = true; break; case Keys.Left: handled = dropDown.__CloseKeyboardLevel(); break; case Keys.Enter: case Keys.Space: __ActivateHotItem(); handled = true; break; case Keys.Escape: dropDown.__EscapeMenuLevel(); handled = true; break; } } if (handled) e.Handled = true; base.OnKeyDown(e); }");
            Add("protected override void Dispose(bool disposing) { if (disposing) { __CancelHoverAction(); if (this is ToolStripDropDown dropDown) dropDown.__CloseDropDown(); } base.Dispose(disposing); }");
            break;
        case "System.Windows.Forms.ToolStripItem":
            Add("private string __itemText = string.Empty;");
            Add("private string __itemName = string.Empty;");
            Add("private string __toolTipText = string.Empty;");
            Add("private object? __itemTag;");
            Add("private global::System.Drawing.Size __itemSize;");
            Add("private Padding __itemMargin;");
            Add("private global::System.Drawing.Image? __itemImage;");
            Add("private bool __itemVisible = true;");
            Add("private bool __itemEnabled = true;");
            Add("internal ToolStrip? __owner;");
            Add("internal string __ProjectionText { get { return __itemText; } }");
            Add("internal string __ProjectionName { get { return __itemName; } }");
            Add("internal global::System.Drawing.Image? __ProjectionImage { get { return __itemImage; } }");
            Add("internal global::System.Drawing.Size __ProjectionSize { get { return __itemSize; } }");
            Add("internal Padding __ProjectionMargin { get { return __itemMargin; } }");
            Add("internal bool __ProjectionVisible { get { return __itemVisible; } }");
            Add("internal bool __ProjectionEnabled { get { return __itemEnabled; } }");
            Add("internal string __ProjectionToolTip { get { return __toolTipText; } }");
            Add("private void __RefreshOwner() { __owner?.__RefreshItems(); }");
            Add("internal void __PerformClick() { if (__itemEnabled && __itemVisible) Click?.Invoke(this, global::System.EventArgs.Empty); }");
            break;
        case "System.Windows.Forms.ToolStripDropDownItem":
            Add("private readonly ToolStripDropDown __dropDown;");
            break;
        case "System.Windows.Forms.ToolStripDropDown":
            Add("private ToolStripItem? __ownerItem;");
            Add("private ToolStripDropDown? __parentDropDown;");
            Add("private ToolStripDropDown? __childDropDown;");
            Add("private bool __nativePopupActive;");
            Add("private bool __dropDownShown;");
            Add("internal event global::System.Action? __DropDownClosed;");
            Add("internal ToolStripDropDown __MenuRoot { get { var root = this; while (root.__parentDropDown is not null) root = root.__parentDropDown; return root; } }");
            Add("internal ToolStripDropDown __KeyboardTarget { get { var target = this; while (target.__childDropDown is not null && target.__childDropDown.Visible) target = target.__childDropDown; return target; } }");
            Add("internal bool __HasVisibleChild { get { return __childDropDown is not null && __childDropDown.Visible; } }");
            Add("internal bool __ContainsMenuTarget(Control target) { for (Control? candidate = target; candidate is not null; candidate = candidate.Parent) for (ToolStripDropDown? menu = this; menu is not null; menu = menu.__childDropDown) if (global::System.Object.ReferenceEquals(candidate, menu)) return true; return false; }");
            Add("private Form? __OverlayForm(Control? ownerControl) { if (Name.Length == 0) Name = ownerControl is not null && ownerControl.Name.Length != 0 ? ownerControl.Name + \".DropDown\" : __ownerItem?.Owner?.Name + \".DropDown\" ?? \"dropDown\"; __TraceToolItems(); return ownerControl?.FindForm() ?? __ownerItem?.Owner?.FindForm() ?? Application.__CurrentForm; }");
            Add("internal void __ShowChildDropDown(ToolStripDropDown child, global::System.Drawing.Point screenLocation, bool focusChild = false) { if (global::System.Object.ReferenceEquals(__childDropDown, child) && child.Visible) { if (focusChild) child.__FocusFirstItem(); return; } __childDropDown?.__CloseDropDown(); __childDropDown = child; child.__parentDropDown = this; child.__ShowDropDown(screenLocation, this); if (focusChild) child.__FocusFirstItem(); }");
            Add("internal void __CloseChildDropDown() { var child = __childDropDown; __childDropDown = null; child?.__CloseDropDown(); }");
            Add("internal bool __CloseKeyboardLevel() { var parent = __parentDropDown; if (parent is null) return false; __CloseDropDown(); parent.Focus(); return true; }");
            Add("internal void __EscapeMenuLevel() { var parent = __parentDropDown; if (parent is null) { __CloseDropDown(); return; } __CloseDropDown(); parent.Focus(); }");
            Add("internal void __CloseDropDown() { var wasRoot = __parentDropDown is null; var wasShown = __dropDownShown; __dropDownShown = false; var child = __childDropDown; __childDropDown = null; child?.__CloseDropDown(); if (__parentDropDown is not null && global::System.Object.ReferenceEquals(__parentDropDown.__childDropDown, this)) __parentDropDown.__childDropDown = null; __parentDropDown = null; Visible = false; var parent = Parent; if (parent is not null) parent.Controls.Remove(this); if (wasRoot) Application.__MenuClosed(this); if (__nativePopupActive) __RequestClose(); if (wasShown) __DropDownClosed?.Invoke(); }");
            Add("internal void __CloseMenuChain() { var root = this; while (root.__parentDropDown is not null) root = root.__parentDropDown; root.__CloseDropDown(); }");
            Add("internal void __ShowDropDown(global::System.Drawing.Point screenLocation, Control? ownerControl = null) { if (Items.Count == 0) return; var form = __OverlayForm(ownerControl); if (__parentDropDown is null) Application.__RegisterMenu(this); __dropDownShown = true; if (form is not null && global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_NATIVE_POPUP_MENUS\") != \"1\") { var clientLocation = form.PointToClient(screenLocation); __PrepareDropDown(clientLocation); var x = clientLocation.X; var y = clientLocation.Y; if (ownerControl is ToolStripDropDown parentDropDown && x + Width > form.Width) x = parentDropDown.Left - Width; x = global::System.Math.Clamp(x, 0, global::System.Math.Max(0, form.Width - Width)); y = global::System.Math.Clamp(y, 0, global::System.Math.Max(0, form.Height - Height)); Bounds = new global::System.Drawing.Rectangle(x, y, Width, Height); if (!global::System.Object.ReferenceEquals(Parent, form)) { Parent?.Controls.Remove(this); form.Controls.Add(this); } __RaiseLoad(); Visible = true; BringToFront(); __RenderManagedPaint(); return; } var work = Screen.GetWorkingArea(screenLocation); __PrepareDropDown(screenLocation); var screenX = global::System.Math.Clamp(screenLocation.X, work.Left, global::System.Math.Max(work.Left, work.Right - Width)); var screenY = global::System.Math.Clamp(screenLocation.Y, work.Top, global::System.Math.Max(work.Top, work.Bottom - Height)); Bounds = new global::System.Drawing.Rectangle(screenX, screenY, Width, Height); _ = BeginInvoke((global::System.Action)(() => { __RaiseLoad(); __RenderManagedPaint(); Visible = true; })); __nativePopupActive = true; try { _ = __RunNativePopup(); } finally { __nativePopupActive = false; Visible = false; if (__parentDropDown is null) Application.__MenuClosed(this); if (__dropDownShown) { __dropDownShown = false; __DropDownClosed?.Invoke(); } } }");
            break;
        case "System.Windows.Forms.ToolStripProfessionalRenderer":
            Add("private ProfessionalColorTable __colorTable = new();");
            Add("private bool __roundedEdges = true;");
            break;
        case "System.Windows.Forms.ToolStripButton":
            Add("private bool __toolButtonChecked;");
            break;
        case "System.Windows.Forms.ToolStripManager":
            Add("private static ToolStripManagerRenderMode __managerRenderMode = ToolStripManagerRenderMode.Professional;");
            Add("private static ToolStripRenderer __managerRenderer = new ToolStripProfessionalRenderer();");
            break;
        case "System.Windows.Forms.ToolStripItemCollection":
            Add("private readonly global::System.Collections.Generic.List<ToolStripItem> __items = new();");
            Add("private readonly ToolStrip? __owner;");
            Add("internal ToolStripItemCollection(ToolStrip owner) { __owner = owner; }");
            Add("public override int Count { get { return __items.Count; } }");
            Add("public override global::System.Collections.IEnumerator GetEnumerator() { return __items.GetEnumerator(); }");
            break;
        case "System.Windows.Forms.ToolStripMenuItem":
            Add("private bool __menuChecked;");
            break;
        case "System.Windows.Forms.Padding":
            Add("private int __left;");
            Add("private int __top;");
            Add("private int __right;");
            Add("private int __bottom;");
            break;
        case "System.Windows.Forms.PaintEventArgs":
            Add("private global::System.Drawing.Graphics? __graphics;");
            Add("private global::System.Drawing.Rectangle __clipRectangle;");
            Add("internal PaintEventArgs(global::System.Drawing.Graphics graphics, global::System.Drawing.Rectangle clipRectangle) { __graphics = graphics; __clipRectangle = clipRectangle; }");
            break;
        case "System.Windows.Forms.InvalidateEventArgs":
            Add("private global::System.Drawing.Rectangle __invalidRect;");
            break;
        case "System.Windows.Forms.LayoutEventArgs":
            Add("private global::System.ComponentModel.IComponent? __affectedComponent;");
            Add("private string? __affectedProperty;");
            break;
        case "System.Windows.Forms.MouseEventArgs":
            Add("private MouseButtons __button;");
            Add("private int __clicks;");
            Add("private int __x;");
            Add("private int __y;");
            Add("private int __delta;");
            break;
        case "System.Windows.Forms.Message":
            Add("private nint __hWnd;");
            Add("private int __msg;");
            Add("private nint __wParam;");
            Add("private nint __lParam;");
            Add("private nint __result;");
            Add("public static Message Create(nint hWnd, int msg, nint wParam, nint lParam) { return new Message { __hWnd = hWnd, __msg = msg, __wParam = wParam, __lParam = lParam }; }");
            break;
        case "System.Windows.Forms.KeyEventArgs":
            Add("private Keys __keyData;");
            Add("private bool __handled;");
            Add("internal bool __Handled { get { return __handled; } }");
            break;
        case "System.Windows.Forms.Form":
            Add("private enum FormPresentationPhase { constructed, initializing, ready, closing, closed }");
            Add("private FormPresentationPhase __presentationPhase;");
            Add("private DialogResult __dialogResult;");
            Add("private bool __hostedNonModal;");
            Add("private bool __modal;");
            Add("private bool __closing;");
            Add("private bool __closedRaised;");
            Add("private bool __ownerWasEnabled;");
            Add("private Control? __previousFocus;");
            Add("private readonly global::System.Collections.Generic.List<Form> __ownedForms = new();");
            Add("private bool __controlBox = true;");
            Add("private bool __topLevel = true;");
            Add("private bool __rightToLeftLayout;");
            Add("private FormWindowState __windowState;");
            Add("private Form? __mdiParent;");
            Add("private MenuStrip? __mainMenuStrip;");
            Add("private global::System.Drawing.Icon __formIcon = __CreateDefaultFormIcon();");
            Add("private Panel? __hostedCaptionPanel;");
            Add("private Label? __hostedCaptionLabel;");
            Add("private Button? __hostedCaptionClose;");
            Add("private Control? __hostedProjectionRoot;");
            Add("private bool __hostedProjectionAttached;");
            Add("private Padding __hostedProjectionOriginalPadding;");
            Add("private bool __hostedProjectionInsetApplied;");
            Add("private bool __raisingHostedProjections;");
            Add("private IButtonControl? __acceptButton;");
            Add("private IButtonControl? __cancelButton;");
            Add("private readonly CreateParams __createParams = new();");
            Add("private FormBorderStyle __formBorderStyle = FormBorderStyle.Sizable;");
            Add("private bool __keyPreview;");
            Add("private bool __maximizeBox = true;");
            Add("private bool __minimizeBox = true;");
            Add("private double __opacity = 1d;");
            Add("private Form? __ownerForm;");
            Add("private bool __showIcon = true;");
            Add("private bool __showInTaskbar = true;");
            Add("private SizeGripStyle __sizeGripStyle;");
            Add("private FormStartPosition __startPosition;");
            Add("private bool __topMost;");
            Add("internal int __HostedCaptionHeight => __hostedNonModal && __formBorderStyle != FormBorderStyle.None ? 25 : 0;");
            Add("private sealed class HostedCaptionSurface : Panel { private readonly Form __owner; private bool __dragging; private bool __closePressed; private global::System.Drawing.Point __dragOrigin; private global::System.Drawing.Rectangle __windowOrigin; internal HostedCaptionSurface(Form owner) { __owner = owner; } protected override void OnPaint(global::System.Windows.Forms.PaintEventArgs e) { using var background = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(42, 45, 52)); using var closeBackground = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(58, 62, 70)); using var ink = new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromArgb(238, 241, 245)); using var stroke = new global::System.Drawing.Pen(global::System.Drawing.Color.FromArgb(242, 244, 247), 1.5f); using var font = new global::System.Drawing.Font(\"Portsmouth Rapids\", 11f, global::System.Drawing.FontStyle.Regular, global::System.Drawing.GraphicsUnit.Pixel, 1); e.Graphics.FillRectangle(background, ClientRectangle); var closeWidth = global::System.Math.Min(__owner.__HostedCaptionHeight, Width); e.Graphics.FillRectangle(closeBackground, global::System.Math.Max(0, Width - closeWidth), 0, closeWidth, Height); e.Graphics.DrawString(__owner.Text, font, ink, 8f, 6f); var centerX = Width - closeWidth / 2f; var centerY = Height / 2f; e.Graphics.DrawLine(stroke, centerX - 4f, centerY - 4f, centerX + 4f, centerY + 4f); e.Graphics.DrawLine(stroke, centerX + 4f, centerY - 4f, centerX - 4f, centerY + 4f); } protected override void OnMouseDown(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseDown(e); if (e.Button != MouseButtons.Left) return; Capture = true; if (e.X >= Width - __owner.__HostedCaptionHeight) { __closePressed = true; return; } __dragging = true; __dragOrigin = Control.MousePosition; __windowOrigin = __owner.Bounds; } protected override void OnMouseMove(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseMove(e); if (!__dragging) return; var current = Control.MousePosition; __owner.__MoveHostedWindow(__windowOrigin.X + current.X - __dragOrigin.X, __windowOrigin.Y + current.Y - __dragOrigin.Y); } protected override void OnMouseUp(global::System.Windows.Forms.MouseEventArgs e) { base.OnMouseUp(e); var wasDragging = __dragging; var wasClosePressed = __closePressed; __dragging = false; __closePressed = false; if (Capture) Capture = false; if (!wasDragging && wasClosePressed && e.X >= Width - __owner.__HostedCaptionHeight && e.Y >= 0 && e.Y < Height) __owner.__CloseFromHostedCaption(); } }");
            Add("private void __CloseFromHostedCaption() { var wasVisible = Visible; var message = Message.Create(Handle, 0x0010, global::System.IntPtr.Zero, global::System.IntPtr.Zero); WndProc(ref message); if (wasVisible && Visible) Close(); }");
            Add("internal bool __HasHostedProjection => __hostedProjectionAttached;");
            Add("internal bool __HostedProjectionVisible => __native.Visible;");
            Add("private void __ProjectHostedRoot() { var root = __hostedProjectionRoot; var host = __ownerForm; if (root is null || host is null) return; __ReserveHostedCaptionClient(); var visible = __native.Visible; if (!__hostedProjectionAttached) { host.__native.RemoveChild(__native); __native.AttachOverlay(host.__native); __hostedProjectionAttached = true; } __native.SetProjectedBounds(Bounds); __native.SetProjectedVisible(visible); root.__native.SetProjectedBounds(root.Bounds); root.__native.SetProjectedVisible(root.Visible); if (__hostedCaptionPanel is not null) root.__native.SetChildIndex(__hostedCaptionPanel.__native, 0); }");
            Add("private void __MoveHostedWindow(int x, int y) { var host = __ownerForm; if (host is null) return; var bounds = Bounds; bounds.X = global::System.Math.Clamp(x, 0, global::System.Math.Max(0, host.Width - bounds.Width)); bounds.Y = global::System.Math.Clamp(y, 0, global::System.Math.Max(0, host.Height - bounds.Height)); Bounds = bounds; __ProjectHostedRoot(); __hostedCaptionPanel?.Invalidate(); }");
            Add("private void __RaiseOwnedHostedProjections() { if (__raisingHostedProjections) return; __raisingHostedProjections = true; try { foreach (var child in __ownedForms.ToArray()) if (child.__hostedNonModal && child.Visible) child.__ProjectHostedRoot(); } finally { __raisingHostedProjections = false; } }");
            Add("internal void __SetHostedProjectionVisible(bool value) { if (!__hostedProjectionAttached) return; __native.SetProjectedVisible(value); }");
            Add("private void __RestoreHostedRootProjection() { var host = __ownerForm; if (__hostedProjectionAttached) { __native.DetachOverlay(); if (host is not null && global::System.Object.ReferenceEquals(Parent, host)) host.__native.AddChild(__native); __hostedProjectionAttached = false; } }");
            Add("private void __RestoreHostedCaptionInset() { if (!__hostedProjectionInsetApplied) return; __hostedProjectionInsetApplied = false; var surface = __hostedProjectionRoot; if (surface is not null) surface.Padding = __hostedProjectionOriginalPadding; }");
            Add("private void __ReserveHostedCaptionClient() { var height = __HostedCaptionHeight; var surface = __hostedCaptionPanel?.Parent; if (height == 0 || surface is null) return; foreach (Control child in surface.Controls) { if (global::System.Object.ReferenceEquals(child, __hostedCaptionPanel) || !child.Visible || child.Top >= height) continue; var bounds = child.Bounds; var bottom = bounds.Bottom; bounds.Y = height; bounds.Height = global::System.Math.Max(0, bottom - height); child.__SetLayoutBounds(bounds); } }");
            Add("private void __ApplyHostedCaptionLayout() { var height = __HostedCaptionHeight; var surface = __hostedCaptionPanel?.Parent; if (height == 0 || surface is null) return; if (!__hostedProjectionInsetApplied) { __hostedProjectionOriginalPadding = surface.Padding; __hostedProjectionInsetApplied = true; surface.Padding = new Padding(__hostedProjectionOriginalPadding.Left, __hostedProjectionOriginalPadding.Top + height, __hostedProjectionOriginalPadding.Right, __hostedProjectionOriginalPadding.Bottom); } __hostedCaptionPanel!.Bounds = new global::System.Drawing.Rectangle(0, 0, surface.Width, height); __hostedCaptionLabel!.Bounds = new global::System.Drawing.Rectangle(8, 0, global::System.Math.Max(0, surface.Width - height - 12), height); __hostedCaptionClose!.Bounds = new global::System.Drawing.Rectangle(global::System.Math.Max(0, surface.Width - height), 0, global::System.Math.Min(height, surface.Width), height); surface.PerformLayout(); __ReserveHostedCaptionClient(); __ProjectHostedRoot(); }");
            Add("private void __EnsureHostedCaptionProjection() { var height = __HostedCaptionHeight; if (height == 0 || Controls.Count == 0) return; var surface = Controls[0]; __hostedProjectionRoot = surface; if (__hostedCaptionPanel is null) { __hostedCaptionPanel = new HostedCaptionSurface(this) { Name = \"__guiFormsHostedCaption\", BackColor = global::System.Drawing.Color.FromArgb(42, 45, 52) }; __hostedCaptionLabel = new Label { Name = \"__guiFormsHostedCaptionText\", Visible = false }; __hostedCaptionClose = new Button { Name = \"__guiFormsHostedCaptionClose\", Text = \"×\", BackColor = global::System.Drawing.Color.FromArgb(58, 62, 70), ForeColor = global::System.Drawing.Color.FromArgb(238, 241, 245), FlatStyle = FlatStyle.Flat, TabStop = false }; __hostedCaptionClose.FlatAppearance.BorderSize = 0; __hostedCaptionClose.Click += (_, _) => __CloseFromHostedCaption(); __hostedCaptionPanel.Controls.Add(__hostedCaptionLabel); __hostedCaptionPanel.Controls.Add(__hostedCaptionClose); __hostedCaptionClose.BringToFront(); surface.Controls.Add(__hostedCaptionPanel); } __hostedCaptionLabel!.Text = Text; __hostedCaptionPanel.Visible = true; __hostedCaptionClose!.Visible = true; __hostedCaptionPanel.Invalidate(); __ApplyHostedCaptionLayout(); }");
            Add("private static global::System.Drawing.Icon __CreateDefaultFormIcon()");
            Add("{");
            Add("const int width = 32, height = 32, pixelOffset = 62, xorBytes = width * height * 4, maskBytes = 128;");
            Add("var data = new byte[pixelOffset + xorBytes + maskBytes];");
            Add("static void Put16(byte[] target, int offset, ushort value) { target[offset] = (byte)value; target[offset + 1] = (byte)(value >> 8); }");
            Add("static void Put32(byte[] target, int offset, uint value) { target[offset] = (byte)value; target[offset + 1] = (byte)(value >> 8); target[offset + 2] = (byte)(value >> 16); target[offset + 3] = (byte)(value >> 24); }");
            Add("Put16(data, 2, 1); Put16(data, 4, 1);");
            Add("data[6] = width; data[7] = height; Put16(data, 10, 1); Put16(data, 12, 32); Put32(data, 14, (uint)(40 + xorBytes + maskBytes)); Put32(data, 18, 22);");
            Add("Put32(data, 22, 40); Put32(data, 26, width); Put32(data, 30, height * 2); Put16(data, 34, 1); Put16(data, 36, 32); Put32(data, 42, xorBytes);");
            Add("for (var storageY = 0; storageY < height; ++storageY) for (var x = 0; x < width; ++x)");
            Add("{");
            Add("var y = height - 1 - storageY; var inside = x >= 3 && x <= 28 && y >= 4 && y <= 27; if (!inside) continue;");
            Add("var border = x == 3 || x == 28 || y == 4 || y == 27; var title = !border && y <= 9; var offset = pixelOffset + (storageY * width + x) * 4;");
            Add("data[offset] = border ? (byte)70 : title ? (byte)190 : (byte)238; data[offset + 1] = border ? (byte)70 : title ? (byte)125 : (byte)238; data[offset + 2] = border ? (byte)70 : title ? (byte)45 : (byte)238; data[offset + 3] = 255;");
            Add("}");
            Add("return new global::System.Drawing.Icon(new global::System.IO.MemoryStream(data, writable: false));");
            Add("}");
            Add("private void __SetOwner(Form? value) { if (global::System.Object.ReferenceEquals(value, this)) throw new global::System.ArgumentException(\"A form cannot own itself.\", nameof(value)); for (var current = value; current is not null; current = current.__ownerForm) if (global::System.Object.ReferenceEquals(current, this)) throw new global::System.ArgumentException(\"Owned-form cycles are not permitted.\", nameof(value)); if (global::System.Object.ReferenceEquals(__ownerForm, value)) return; __ownerForm?.__ownedForms.Remove(this); __ownerForm = value; if (value is not null && !value.__ownedForms.Contains(this)) value.__ownedForms.Add(this); }");
            Add("private bool __RaiseFormClosing(CloseReason reason) { if (__presentationPhase == FormPresentationPhase.closed || __closedRaised) return false; if (__presentationPhase == FormPresentationPhase.closing || __closing) return true; __presentationPhase = FormPresentationPhase.closing; var args = new FormClosingEventArgs(); args.__SetCloseReason(reason); OnFormClosing(args); if (args.Cancel) { __presentationPhase = FormPresentationPhase.ready; return true; } __closing = true; return false; }");
            Add("private void __RaiseFormClosed(CloseReason reason) { if (__closedRaised) return; __closedRaised = true; __closing = false; __presentationPhase = FormPresentationPhase.closed; var args = new FormClosedEventArgs(); args.__SetCloseReason(reason); FormClosed?.Invoke(this, args); }");
            Add("private void __CloseOwnedForms(CloseReason reason) { foreach (var child in __ownedForms.ToArray()) child.__CloseFromOwner(reason); }");
            Add("private void __CloseFromOwner(CloseReason reason) { if (!Visible || __RaiseFormClosing(reason)) return; __CloseOwnedForms(reason); var parent = Parent; Visible = false; Control.__ClearFocusWithin(this); parent?.Controls.Remove(this); __hostedNonModal = false; __RaiseFormClosed(reason); }");
            Add("private void __EnsureInitialFocus() { var focused = Control.__FocusedControl; if (focused is null || (!global::System.Object.ReferenceEquals(focused, this) && !__ContainsDescendant(focused))) __SelectNextDescendant(null, true); }");
            Add("private bool __NativeKeyPreview(NativeKey input) { if (__DeferManagedInput(() => { _ = __NativeKeyPreview(input); })) return true; var key = Control.__KeysFromNative(input.PhysicalKey, input.Modifiers); if (__keyPreview) { var args = new KeyEventArgs(key); if (input.Kind == 11u) OnKeyDown(args); else OnKeyUp(args); if (args.__Handled) return true; } return input.Kind == 11u && __ProcessDialogKey(key); }");
            Add("internal bool __ProcessDialogKey(Keys keyData) { var code = keyData & Keys.KeyCode; var modifiers = keyData & Keys.Modifiers; if (code == Keys.Tab && (modifiers & (Keys.Control | Keys.Alt)) == 0) return __SelectNextDescendant(ActiveControl, (modifiers & Keys.Shift) == 0); if (code == Keys.Enter && modifiers == Keys.None && __acceptButton is not null) { if (__acceptButton is Control control && (!control.Enabled || !control.Visible)) return false; __acceptButton.PerformClick(); return true; } if (code == Keys.Escape && modifiers == Keys.None && __cancelButton is not null) { if (__cancelButton is Control control && (!control.Enabled || !control.Visible)) return false; __cancelButton.PerformClick(); return true; } return false; }");
            Add("private DialogResult __ShowDialogCore(Form? explicitOwner) { if (__modal || __hostedNonModal || __presentationPhase is FormPresentationPhase.initializing or FormPresentationPhase.ready or FormPresentationPhase.closing) throw new global::System.InvalidOperationException(\"A visible form cannot be shown modally.\"); var previousActiveForm = Application.__CurrentForm; var owner = explicitOwner ?? __ownerForm ?? (global::System.Object.ReferenceEquals(previousActiveForm, this) ? null : previousActiveForm); if (owner is not null) __SetOwner(owner); Application.__CloseActiveMenu(); __dialogResult = DialogResult.None; __closing = false; __closedRaised = false; __modal = true; __previousFocus = Control.__FocusedControl; if (owner is not null) { __ownerWasEnabled = owner.Enabled; owner.Enabled = false; Control.__ClearFocusWithin(owner); } Application.__CurrentForm = this; try { __QueueInitialShow(); _ = __RunNativeWindow(); } finally { Application.__CloseActiveMenu(); Visible = false; Control.__ClearFocusWithin(this); __modal = false; Application.__CurrentForm = previousActiveForm; if (owner is not null) owner.Enabled = __ownerWasEnabled; Control.__RestoreFocus(__previousFocus); if (__TraceLifecycle) global::System.Console.Error.WriteLine(\"facade-dialog=closed|type=\" + GetType().FullName + \"|result=\" + __dialogResult); } return __dialogResult; }");
            Add("public DialogResult ShowDialog(IWin32Window owner) { if (owner is null) throw new global::System.ArgumentNullException(nameof(owner)); if (owner is not Form form) throw new global::System.ArgumentException(\"GUI.Forms modal ownership requires a Form owner.\", nameof(owner)); return __ShowDialogCore(form); }");
            Add("public Form[] OwnedForms { get { return __ownedForms.ToArray(); } }");
            break;
        case "System.Windows.Forms.IButtonControl":
            Add("DialogResult DialogResult { get; set; }");
            Add("void NotifyDefault(bool value);");
            Add("void PerformClick();");
            break;
    }
}

static string ConstructorBody(Type type, ConstructorInfo? constructor)
{
    var count = constructor?.GetParameters().Length ?? 0;
    return type.FullName switch
    {
        "System.Drawing.Bitmap" when count is 2 or 3 => "{ if (width <= 0) throw new global::System.ArgumentOutOfRangeException(nameof(width)); if (height <= 0) throw new global::System.ArgumentOutOfRangeException(nameof(height)); __width = width; __height = height; __pixelFormat = " + (count == 3 ? "format" : "global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb") + "; __bitmap = NativeDrawingBridge.BitmapCreate(width, height); }",
        "System.Drawing.Bitmap" when count == 1 && constructor!.GetParameters()[0].ParameterType.FullName == "System.IO.Stream" => "{ if (stream is null) throw new global::System.ArgumentNullException(nameof(stream)); using var copy = new global::System.IO.MemoryStream(); stream.CopyTo(copy); var decoded = NativeDrawingBridge.DecodePng(copy.ToArray()); __width = decoded.__width; __height = decoded.__height; __pixelFormat = decoded.__pixelFormat; __bitmap = decoded.__bitmap; decoded.__bitmap = default; decoded.Dispose(); }",
        "System.Drawing.Bitmap" when count == 1 => "{ if (original is null) throw new global::System.ArgumentNullException(nameof(original)); __width = original.Width; __height = original.Height; __pixelFormat = original.PixelFormat; __bitmap = NativeDrawingBridge.BitmapClone(original.__BitmapHandle, 0, 0, __width, __height); }",
        "System.Drawing.Icon" when count == 1 => "{ if (stream is null) throw new global::System.ArgumentNullException(nameof(stream)); __iconData = NativeDrawingBridge.ReadBounded(stream, \"ICO\"); }",
        "System.Drawing.SolidBrush" when count == 1 => "{ __color = color; __handle = NativeDrawingBridge.SolidBrushCreate(color); }",
        "System.Drawing.Pen" when count is 1 or 2 && constructor!.GetParameters()[0].ParameterType.FullName == "System.Drawing.Color" => "{ __width = " + (count == 2 ? "width" : "1f") + "; __handle = NativeDrawingBridge.PenCreate(color, __width); }",
        "System.Drawing.Pen" when count is 1 or 2 => "{ if (brush is null) throw new global::System.ArgumentNullException(nameof(brush)); __width = " + (count == 2 ? "width" : "1f") + "; __brushHandle = brush.__handle; __handle = NativeDrawingBridge.PenCreate(brush.__color, __width); }",
        "System.Drawing.Font" when count == 2 && constructor!.GetParameters()[0].ParameterType.FullName == "System.String" => "{ __family = familyName ?? throw new global::System.ArgumentNullException(nameof(familyName)); __size = emSize; __handle = NativeDrawingBridge.FontCreate(__family, __size, __style, __unit, 1); }",
        "System.Drawing.Font" when count == 2 => "{ if (prototype is null) throw new global::System.ArgumentNullException(nameof(prototype)); __family = prototype.__family; __size = prototype.__size; __style = newStyle; __unit = prototype.__unit; __handle = NativeDrawingBridge.FontCreate(__family, __size, __style, __unit, 1); }",
        "System.Drawing.Font" when count == 5 => "{ __family = familyName ?? throw new global::System.ArgumentNullException(nameof(familyName)); __size = emSize; __style = style; __unit = unit; __handle = NativeDrawingBridge.FontCreate(__family, __size, __style, __unit, gdiCharSet); }",
        "System.Drawing.FontFamily" when count == 1 => "{ __name = name ?? throw new global::System.ArgumentNullException(nameof(name)); }",
        "System.Drawing.StringFormat" when count == 0 => "{ __handle = NativeDrawingBridge.StringFormatCreate(0); }",
        "System.Drawing.StringFormat" when count == 1 && constructor!.GetParameters()[0].ParameterType.FullName == "System.Drawing.StringFormatFlags" => "{ __flags = options; __handle = NativeDrawingBridge.StringFormatCreate((uint)options); }",
        "System.Drawing.StringFormat" when count == 1 => "{ if (format is null) throw new global::System.ArgumentNullException(nameof(format)); __alignment = format.__alignment; __lineAlignment = format.__lineAlignment; __trimming = format.__trimming; __flags = format.__flags; __handle = NativeDrawingBridge.StringFormatCreate((uint)__flags); NativeDrawingBridge.StringFormatSet(__handle, __alignment, __lineAlignment, __trimming, __flags); }",
        // Bind an empty recorder to the thread that first uses Graphics. retired compatibility specimen
        // constructs some Graphics instances before handing them to its paint
        // thread; post-first-use operations remain owner-thread enforced.
        "System.Drawing.Graphics" => "{ }",
        "System.Drawing.Design.PaintValueEventArgs" when count == 4 => "{ __paintValueContext = context; __paintValue = value; __paintValueGraphics = graphics ?? throw new global::System.ArgumentNullException(nameof(graphics)); __paintValueBounds = bounds; }",
        "System.Drawing.Drawing2D.GraphicsPath" => "{ __handle = global::System.Drawing.NativeDrawingBridge.GraphicsPathCreate(); }",
        "System.Drawing.Drawing2D.Matrix" when count == 2 => "{ if (rect.Width == 0 || rect.Height == 0) throw new global::System.ArgumentException(\"Matrix source rectangle is empty.\", nameof(rect)); if (plgpts is null || plgpts.Length != 3) throw new global::System.ArgumentException(\"Matrix parallelogram requires three points.\", nameof(plgpts)); __m11 = (plgpts[1].X - plgpts[0].X) / (float)rect.Width; __m12 = (plgpts[1].Y - plgpts[0].Y) / (float)rect.Width; __m21 = (plgpts[2].X - plgpts[0].X) / (float)rect.Height; __m22 = (plgpts[2].Y - plgpts[0].Y) / (float)rect.Height; __dx = plgpts[0].X - rect.X * __m11 - rect.Y * __m21; __dy = plgpts[0].Y - rect.X * __m12 - rect.Y * __m22; }",
        "System.Drawing.Drawing2D.HatchBrush" when count == 3 => "{ __color = foreColor; __handle = global::System.Drawing.NativeDrawingBridge.HatchBrushCreate((uint)hatchstyle, foreColor, backColor); }",
        // GDI+ includes the far raster endpoint when a gradient-backed Pen
        // draws an inclusive line. Skia's repeat shader samples the final pixel
        // just beyond t=1 and wraps it to the high-amplitude color. Start with a
        // clamped shader so the default in-bounds rendering matches GDI+; an
        // explicit WrapMode assignment still selects Tile/TileFlip normally.
        "System.Drawing.Drawing2D.LinearGradientBrush" when count == 4 => "{ var bounds = new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height); var angle = linearGradientMode switch { global::System.Drawing.Drawing2D.LinearGradientMode.Vertical => 90f, global::System.Drawing.Drawing2D.LinearGradientMode.ForwardDiagonal => 45f, global::System.Drawing.Drawing2D.LinearGradientMode.BackwardDiagonal => 135f, _ => 0f }; __color = color1; __handle = global::System.Drawing.NativeDrawingBridge.LinearGradientBrushCreate(bounds, color1, color2, angle, global::System.Drawing.Drawing2D.WrapMode.Clamp); }",
        "System.Drawing.Drawing2D.PathGradientBrush" when count == 1 => "{ if (path is null) throw new global::System.ArgumentNullException(nameof(path)); __handle = global::System.Drawing.NativeDrawingBridge.PathGradientBrushCreate(global::System.Drawing.NativeDrawingBridge.GraphicsPathPoints(path.__handle)); }",
        "System.Drawing.Drawing2D.ColorBlend" when count == 1 => "{ if (count < 0) throw new global::System.ArgumentOutOfRangeException(nameof(count)); __colors = new global::System.Drawing.Color[count]; __positions = new float[count]; }",
        "System.Drawing.Imaging.ImageAttributes" => "{ __handle = global::System.Drawing.NativeDrawingBridge.ImageAttributesCreate(); }",
        "System.Drawing.Region" when count == 1 && constructor!.GetParameters()[0].ParameterType.FullName == "System.Drawing.Rectangle" => "{ __handle = NativeDrawingBridge.RegionCreate(new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height)); }",
        "System.Drawing.Region" when count == 1 => "{ if (path is null) throw new global::System.ArgumentNullException(nameof(path)); __handle = NativeDrawingBridge.RegionCreate(path.__handle); }",
        "System.Windows.Forms.InvalidateEventArgs" when count == 1 => "{ __invalidRect = invalidRect; }",
        "System.Windows.Forms.LayoutEventArgs" when count == 2 =>
            constructor!.GetParameters()[0].ParameterType.FullName == "System.Windows.Forms.Control"
                ? "{ __affectedComponent = affectedControl; __affectedProperty = affectedProperty; }"
                : "{ __affectedComponent = affectedComponent; __affectedProperty = affectedProperty; }",
        "System.Windows.Forms.ScrollableControl" => "{ __horizontalScroll = new HScrollProperties(this); __verticalScroll = new VScrollProperties(this); __dockPadding = new DockPaddingEdges(this); __RefreshScrollState(); }",
        "System.Windows.Forms.ScrollEventArgs" when count == 2 => "{ __scrollEventType = type; __scrollNewValue = newValue; }",
        "System.Windows.Forms.ScrollEventArgs" when count == 3 && constructor!.GetParameters()[1].Name == "newValue" => "{ __scrollEventType = type; __scrollNewValue = newValue; __scrollOrientation = scroll; }",
        "System.Windows.Forms.ScrollEventArgs" when count == 3 => "{ __scrollEventType = type; __scrollOldValue = oldValue; __scrollNewValue = newValue; }",
        "System.Windows.Forms.ScrollEventArgs" when count == 4 => "{ __scrollEventType = type; __scrollOldValue = oldValue; __scrollNewValue = newValue; __scrollOrientation = scroll; }",
        "System.Windows.Forms.Control" => "{ __native = NativeControlBridge.Create(GetType()); __native.Changed += __NativeChanged; __native.NativeEventRaised += __NativeEvent; __native.PointerRaised += __NativePointer; __native.KeyRaised += input => __NativeKeyIngress(input.PhysicalKey, input.Kind == 11u, input.Modifiers, input.Repeat); __native.TextRaised += __NativeTextIngress; __controls = new ControlCollection(this); __ApplyEffectiveColors(); }",
        "System.Windows.Forms.Form" => "{ __AttachKeyPreview(__NativeKeyPreview); }",
        "System.Windows.Forms.ButtonBase" => "{ __flatAppearance.__Attach(this); __ApplyNativeButtonAppearance(); }",
        "System.Windows.Forms.UserControl" => "{ __SetInitialSize(new global::System.Drawing.Size(150, 150)); }",
        "System.Windows.Forms.Button" => "{ Size = new global::System.Drawing.Size(75, 23); }",
        "System.Windows.Forms.ListBox" => "{ __listItems = new ObjectCollection(this); }",
        "System.Windows.Forms.SplitContainer" => "{ Cursor = Cursors.VSplit; Controls.Add(__panel1); Controls.Add(__panel2); }",
        "System.Windows.Forms.TableLayoutPanel" => "{ __tableControls = new TableLayoutControlCollection(this); __controls = __tableControls; }",
        // WinForms exposes the spinner before the edit in this composite.
        "System.Windows.Forms.UpDownBase" => "{ Controls.Add(new Button { Name = \"upDownButtons\", Visible = false }); Controls.Add(new TextBox { Name = \"upDownEdit\", Visible = false }); }",
        "System.Windows.Forms.NumericUpDown" => "{ __UpdateNumericText(); }",
        "System.Windows.Forms.ComboBox" => "{ __comboItems = new ObjectCollection(this); }",
        "System.Windows.Forms.DataGridView" => "{ __gridColumns = new DataGridViewColumnCollection(this); __gridRows = new DataGridViewRowCollection(this); }",
        "System.Windows.Forms.DataGridViewColumn" when count == 1 => "{ __cellTemplate = cellTemplate ?? throw new global::System.ArgumentNullException(nameof(cellTemplate)); }",
        "System.Windows.Forms.BindingSource" when count == 1 => "{ container?.Add(this); }",
        "System.Windows.Forms.ToolStrip" => "{ __toolItems = new ToolStripItemCollection(this); }",
        "System.Windows.Forms.ToolStripDropDownItem" => "{ __dropDown = new ToolStripDropDown { OwnerItem = this }; }",
        "System.Windows.Forms.ToolStripProfessionalRenderer" when count == 1 => "{ __colorTable = professionalColorTable ?? throw new global::System.ArgumentNullException(nameof(professionalColorTable)); }",
        "System.Windows.Forms.RowStyle" when count == 1 => "{ __sizeType = sizeType; }",
        "System.Windows.Forms.RowStyle" when count == 2 => "{ __sizeType = sizeType; __size = height; }",
        "System.Windows.Forms.ColumnStyle" when count == 1 => "{ __sizeType = sizeType; }",
        "System.Windows.Forms.ColumnStyle" when count == 2 => "{ __sizeType = sizeType; __size = width; }",
        "System.Windows.Forms.Padding" when count == 1 => "{ __left = __top = __right = __bottom = all; }",
        "System.Windows.Forms.Padding" when count == 4 => "{ __left = left; __top = top; __right = right; __bottom = bottom; }",
        "System.Windows.Forms.MouseEventArgs" when count == 5 => "{ __button = button; __clicks = clicks; __x = x; __y = y; __delta = delta; }",
        "System.Windows.Forms.KeyEventArgs" when count == 1 => "{ __keyData = keyData; }",
        "System.Windows.Forms.Timer" when count == 1 => "{ container?.Add(this); }",
        "System.Windows.Forms.ToolTip" when count == 1 => "{ cont?.Add(this); }",
        "System.Windows.Forms.ToolStripMenuItem" when count == 1 => "{ Text = text ?? string.Empty; }",
        _ when type.IsValueType => "{ this = default; }",
        _ => "{ }",
    };
}

static string PropertyBody(Type type, PropertyInfo property, HashSet<MethodInfo> accessors)
{
    var get = property.GetMethod is not null && accessors.Contains(property.GetMethod);
    var set = property.SetMethod is not null && accessors.Contains(property.SetMethod);
    string Stub() => StubProperty(type, property, type.IsInterface, get, set);
    if (type.FullName == "System.Drawing.Design.PaintValueEventArgs")
        return property.Name switch
        {
            "Bounds" => "{ get { return __paintValueBounds; } }",
            "Context" => "{ get { return __paintValueContext!; } }",
            "Graphics" => "{ get { return __paintValueGraphics!; } }",
            "Value" => "{ get { return __paintValue!; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Drawing.Design.UITypeEditor" &&
        property.Name == "IsDropDownResizable")
        return "{ get { return false; } }";
    if (type.FullName == "System.Windows.Forms.Application" && property.Name == "ExecutablePath")
        return "{ get { return global::System.Environment.ProcessPath ?? global::System.AppContext.BaseDirectory; } }";
    if (type.FullName == "System.Windows.Forms.LayoutEventArgs")
        return property.Name switch
        {
            "AffectedComponent" => "{ get { return __affectedComponent; } }",
            "AffectedControl" => "{ get { return __affectedComponent as global::System.Windows.Forms.Control; } }",
            "AffectedProperty" => "{ get { return __affectedProperty; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Cursor")
        return property.Name switch
        {
            "Current" => "{ set { __current = value; } }",
            "Position" => "{ get { return __position; } set { __position = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DateTimePicker")
        return property.Name switch
        {
            "CalendarForeColor" => "{ set { __calendarForeColor = value; } }",
            "CalendarMonthBackground" => "{ set { __calendarMonthBackground = value; } }",
            "CustomFormat" => "{ get { return __customFormat; } set { __customFormat = value ?? string.Empty; } }",
            "Format" => "{ set { __dateTimeFormat = value; } }",
            "MaxDate" => "{ set { if (value < __minimumDate) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __maximumDate = value; if (__dateTimeValue > value) __dateTimeValue = value; } }",
            "MinDate" => "{ set { if (value > __maximumDate) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __minimumDate = value; if (__dateTimeValue < value) __dateTimeValue = value; } }",
            "Value" => "{ get { return __dateTimeValue; } set { if (value < __minimumDate || value > __maximumDate) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __dateTimeValue = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Screen")
        return property.Name switch
        {
            "AllScreens" => "{ get { return new[] { new Screen() }; } }",
            "Bounds" => "{ get { return __screenBounds; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.SystemInformation")
        return property.Name switch
        {
            "DragSize" => "{ get { return new global::System.Drawing.Size(4, 4); } }",
            "HorizontalScrollBarArrowWidth" => "{ get { return 17; } }",
            "MouseHoverTime" => "{ get { return 400; } }",
            "VirtualScreen" => "{ get { return new global::System.Drawing.Rectangle(0, 0, 1920, 1080); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Layout.ArrangedElementCollection" && property.Name == "Count")
        return "{ get { return 0; } }";
    if (type.FullName == "System.Windows.Forms.Label")
        return property.Name switch
        {
            "AutoEllipsis" => "{ set { __autoEllipsis = value; Invalidate(); } }",
            "TextAlign" => "{ set { var compact = __CompactContentAlignment(value); __labelTextAlign = value; if (__native.SupportsRaster) Invalidate(); else __native.SetTextAlignment(compact); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.LinkLabel")
        return property.Name switch
        {
            "Links" => "{ get { return __links; } }",
            "Text" => "{ get { return base.Text; } set { base.Text = value ?? string.Empty; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.LinkLabel+Link")
        return property.Name switch
        {
            "Start" => "{ set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __linkStart = value; } }",
            "Length" => "{ set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __linkLength = value; } }",
            "LinkData" => "{ get { return __linkData!; } set { __linkData = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.LinkLabelLinkClickedEventArgs" && property.Name == "Link")
        return "{ get { return __clickedLink!; } }";
    if (type.FullName == "System.Windows.Forms.ColorDialog")
        return property.Name switch
        {
            "AnyColor" => "{ set { __anyColor = value; } }",
            "Color" => "{ get { return __dialogColor; } set { __dialogColor = value; } }",
            "FullOpen" => "{ set { __fullOpen = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.FileDialog")
        return property.Name switch
        {
            "DefaultExt" => "{ set { __defaultExt = value ?? string.Empty; } }",
            "FileName" => "{ get { return __fileName; } set { __fileName = value ?? string.Empty; } }",
            "Filter" => "{ set { __filter = value ?? string.Empty; } }",
            "InitialDirectory" => "{ get { return __initialDirectory; } set { __initialDirectory = value ?? string.Empty; } }",
            "RestoreDirectory" => "{ set { __restoreDirectory = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.FolderBrowserDialog" && property.Name == "SelectedPath")
        return "{ get { return __selectedPath; } set { __selectedPath = value ?? string.Empty; } }";
    if (type.FullName == "System.Windows.Forms.OpenFileDialog" && property.Name == "SafeFileName")
        return "{ get { return global::System.IO.Path.GetFileName(__fileName); } }";
    if (type.FullName == "System.Windows.Forms.ToolTip")
        return property.Name switch
        {
            "Active" => "{ set { __toolTipActive = value; if (!value) { if (__shownWindow is Control control) __CancelPending(control, true); __shownWindow = null; __shownText = string.Empty; } } }",
            "AutoPopDelay" => "{ set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __autoPopDelay = value; } }",
            "AutomaticDelay" => "{ set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __initialDelay = value; __reshowDelay = value / 5; __autoPopDelay = checked(value * 10); } }",
            "InitialDelay" => "{ set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __initialDelay = value; } }",
            "ReshowDelay" => "{ set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __reshowDelay = value; } }",
            "ShowAlways" => "{ set { __showAlways = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DrawItemEventArgs")
        return property.Name switch
        {
            "Bounds" => "{ get { return __drawBounds; } }",
            "Graphics" => "{ get { return __drawGraphics!; } }",
            "Index" => "{ get { return __drawIndex; } }",
            "State" => "{ get { return __drawState; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ConvertEventArgs" && property.Name == "Value")
        return "{ get { return __convertedValue!; } set { __convertedValue = value; } }";
    if (type.FullName == "System.Windows.Forms.CreateParams" && property.Name == "ExStyle")
        return "{ get { return __extendedStyle; } set { __extendedStyle = value; } }";
    if (type.FullName == "System.Windows.Forms.FormClosingEventArgs" && property.Name == "CloseReason")
        return "{ get { return __closeReason; } }";
    if (type.FullName == "System.Windows.Forms.PreviewKeyDownEventArgs" && property.Name == "KeyCode")
        return "{ get { return __previewKeyCode; } }";
    if (type.FullName == "System.Windows.Forms.ToolStripItemRenderEventArgs")
        return property.Name switch
        {
            "Graphics" => "{ get { return __renderGraphics!; } }",
            "Item" => "{ get { return __renderItem!; } }",
            "ToolStrip" => "{ get { return __renderToolStrip!; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripItemImageRenderEventArgs")
        return property.Name switch
        {
            "Image" => "{ get { return __renderImage!; } }",
            "ImageRectangle" => "{ get { return __renderImageRectangle; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripItemTextRenderEventArgs")
        return property.Name switch
        {
            "Text" => "{ get { return __renderText; } }",
            "TextFont" => "{ get { return __renderTextFont!; } }",
            "TextFormat" => "{ get { return __renderTextFormat; } }",
            "TextRectangle" => "{ get { return __renderTextRectangle; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripRenderEventArgs")
        return property.Name switch
        {
            "Graphics" => "{ get { return __stripGraphics!; } }",
            "ToolStrip" => "{ get { return __strip!; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripArrowRenderEventArgs")
        return property.Name switch
        {
            "ArrowColor" => "{ set { __arrowColor = value; } }",
            "Item" => "{ get { return __arrowItem!; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripGripRenderEventArgs" && property.Name == "GripBounds")
        return "{ get { return __gripBounds; } }";
    if (type.FullName == "System.Windows.Forms.ToolStripSeparatorRenderEventArgs" && property.Name == "Vertical")
        return "{ get { return __verticalSeparator; } }";
    if (type.FullName == "System.Drawing.Image")
        return property.Name switch
        {
            "Width" => "{ get { return __width; } }",
            "Height" => "{ get { return __height; } }",
            "PixelFormat" => "{ get { return __pixelFormat; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Drawing.SolidBrush" && property.Name == "Color")
        return "{ get { return __color; } }";
    if (type.FullName == "System.Drawing.Font" && property.Name == "Height")
        return "{ get { return global::System.Math.Max(1, (int)global::System.Math.Ceiling(NativeDrawingBridge.FontPixelSize(__size, __unit) * 1.2f)); } }";
    if (type.FullName == "System.Drawing.Pen")
        return property.Name switch
        {
            "Width" => "{ set { __width = value; NativeDrawingBridge.PenSetWidth(__handle, value); } }",
            "DashStyle" => "{ set { NativeDrawingBridge.PenSetDashStyle(__handle, (uint)value); } }",
            "DashPattern" => "{ set { NativeDrawingBridge.PenSetDashPattern(__handle, value); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Drawing.Imaging.ImageFormat" && property.Name == "Png")
        return "{ get { return __png; } }";
    if (type.FullName == "System.Drawing.SystemFonts")
        return "{ get { return new global::System.Drawing.Font(\"Lucida Grande\", 12f); } }";
    if (type.FullName == "System.Drawing.Brushes")
        return "{ get { return new global::System.Drawing.SolidBrush(global::System.Drawing.Color.FromName(\"" + property.Name + "\")); } }";
    if (type.FullName == "System.Drawing.Pens")
        return "{ get { return new global::System.Drawing.Pen(global::System.Drawing.Color.FromName(\"" + property.Name + "\")); } }";
    if (type.FullName == "System.Drawing.StringFormat")
        return property.Name switch
        {
            "Alignment" => "{ set { __alignment = value; NativeDrawingBridge.StringFormatSet(__handle, __alignment, __lineAlignment, __trimming, __flags); } }",
            "LineAlignment" => "{ set { __lineAlignment = value; NativeDrawingBridge.StringFormatSet(__handle, __alignment, __lineAlignment, __trimming, __flags); } }",
            "Trimming" => "{ set { __trimming = value; NativeDrawingBridge.StringFormatSet(__handle, __alignment, __lineAlignment, __trimming, __flags); } }",
            "FormatFlags" => "{ get { return __flags; } set { __flags = value; NativeDrawingBridge.StringFormatSet(__handle, __alignment, __lineAlignment, __trimming, __flags); } }",
            "GenericDefault" or "GenericTypographic" => "{ get { return new global::System.Drawing.StringFormat(); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Drawing.Graphics")
        return property.Name switch
        {
            "ClipBounds" => "{ get { return __target is null ? global::System.Drawing.RectangleF.Empty : new global::System.Drawing.RectangleF(0, 0, __target.Width, __target.Height); } }",
            "SmoothingMode" => "{ get { return __smoothing; } set { __smoothing = value; __EnsureRecorder(); NativeDrawingBridge.RecorderQuality(this); } }",
            "InterpolationMode" => "{ get { return __interpolation; } set { __interpolation = value; __EnsureRecorder(); NativeDrawingBridge.RecorderQuality(this); } }",
            "PixelOffsetMode" => "{ set { __pixelOffset = value; __EnsureRecorder(); NativeDrawingBridge.RecorderQuality(this); } }",
            "CompositingMode" => "{ set { __compositing = value; __EnsureRecorder(); NativeDrawingBridge.RecorderQuality(this); } }",
            "CompositingQuality" => "{ set { __compositingQuality = value; __EnsureRecorder(); NativeDrawingBridge.RecorderQuality(this); } }",
            "Transform" => "{ get { return __transform; } set { __transform = value ?? throw new global::System.ArgumentNullException(nameof(value)); __EnsureRecorder(); NativeDrawingBridge.RecorderSetTransform(__recorder, __transform); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Drawing.Drawing2D.GraphicsPath" && property.Name == "PathPoints")
        return "{ get { return global::System.Drawing.NativeDrawingBridge.GraphicsPathPoints(__handle); } }";
    if (type.FullName == "System.Drawing.Drawing2D.ColorBlend")
        return property.Name switch
        {
            "Colors" => "{ get { return __colors; } set { __colors = value ?? throw new global::System.ArgumentNullException(nameof(value)); } }",
            "Positions" => "{ get { return __positions; } set { __positions = value ?? throw new global::System.ArgumentNullException(nameof(value)); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Drawing.Drawing2D.LinearGradientBrush")
        return property.Name switch
        {
            "InterpolationColors" => "{ set { if (value is null) throw new global::System.ArgumentNullException(nameof(value)); global::System.Drawing.NativeDrawingBridge.LinearGradientSetInterpolation(__handle, value.__colors, value.__positions); } }",
            "WrapMode" => "{ set { global::System.Drawing.NativeDrawingBridge.LinearGradientSetWrap(__handle, value); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Drawing.Drawing2D.PathGradientBrush")
        return property.Name switch
        {
            "CenterColor" => "{ set { global::System.Drawing.NativeDrawingBridge.PathGradientSetCenterColor(__handle, value); } }",
            "SurroundColors" => "{ set { global::System.Drawing.NativeDrawingBridge.PathGradientSetSurroundColors(__handle, value); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Drawing.Imaging.ColorMap")
        return property.Name switch
        {
            "OldColor" => "{ set { __oldColor = value; } }",
            "NewColor" => "{ set { __newColor = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Drawing.Imaging.ColorMatrix" && property.Name == "Matrix33")
        return "{ set { __values[18] = value; } }";
    if (type.FullName == "System.Drawing.Imaging.BitmapData")
        return property.Name switch
        {
            "Scan0" => "{ get { return __scan0; } }",
            "Stride" => "{ get { return __stride; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.BaseCollection" && property.Name == "Count")
        return "{ get { return __Collection.Count; } }";
    if (type.FullName == "System.Windows.Forms.Message")
        return property.Name switch
        {
            "HWnd" => "{ get { return __hWnd; } }",
            "Msg" => "{ get { return __msg; } }",
            "WParam" => "{ get { return __wParam; } }",
            "LParam" => "{ get { return __lParam; } }",
            "Result" => "{ set { __result = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.BindingSource")
        return property.Name switch
        {
            "Current" => "{ get { return __position >= 0 && __position < __bindingList.Count ? __bindingList[__position]! : null!; } }",
            "DataSource" => "{ set { __SetDataSource(value); } }",
            "List" => "{ get { return __bindingList; } }",
            "Position" => "{ set { __position = __bindingList.Count == 0 ? -1 : global::System.Math.Clamp(value, -1, __bindingList.Count - 1); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.PropertyGrid")
        return property.Name switch
        {
            "SelectedObject" => "{ get { return __propertyGridSelectedObjects.Length == 0 ? null : __propertyGridSelectedObjects[0]; } set { __SetPropertyGridSelection(value is null ? global::System.Array.Empty<object>() : new object[] { value }); } }",
            "SelectedObjects" => "{ get { return (object[])__propertyGridSelectedObjects.Clone(); } set { __SetPropertyGridSelection(value); } }",
            "PropertySort" => "{ get { return __propertyGridSort; } set { if ((int)value < 0 || (int)value > 3) throw new global::System.ComponentModel.InvalidEnumArgumentException(nameof(value), (int)value, typeof(PropertySort)); if (__propertyGridSort == value) return; __native.SetPropertyGridSort((uint)value); __propertyGridSort = value; PropertySortChanged?.Invoke(this, global::System.EventArgs.Empty); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Control")
    {
        return property.Name switch
        {
            "AllowDrop" => "{ get { return __allowDrop; } set { __allowDrop = value; } }",
            "Anchor" => "{ get { return __anchor; } set { __anchor = value; } }",
            "AutoScrollOffset" => "{ get { return __autoScrollOffset; } set { __autoScrollOffset = value; __native.SetAutoScrollOffset(value); } }",
            "AutoSize" => "{ get { return __autoSize; } set { if (__autoSize == value) return; __autoSize = value; if (value) __RefreshAutoSizeFromChildren(); else __parent?.PerformLayout(); AutoSizeChanged?.Invoke(this, global::System.EventArgs.Empty); } }",
            "BackColor" => "{ get { return !__backColor.IsEmpty ? __backColor : this is TextBoxBase or ComboBox or NumericUpDown ? global::System.Drawing.Color.FromArgb(250, 250, 250) : __parent is not null ? __parent.BackColor : global::System.Drawing.Color.FromArgb(229, 234, 239); } set { if (__backColor == value) return; __backColor = value; __ApplyEffectiveColors(); OnBackColorChanged(global::System.EventArgs.Empty); if (__loadRaised) __QueueManagedPaint(); } }",
            "BackgroundImage" => "{ set { __backgroundImage = value; if (__loadRaised) __RenderManagedPaint(); } }",
            "BackgroundImageLayout" => "{ set { __backgroundImageLayout = value; if (__loadRaised) __RenderManagedPaint(); } }",
            "Bottom" => "{ get { return Bounds.Bottom; } }",
            "Name" => "{ get { return __native.Name; } set { __native.Name = value ?? string.Empty; } }",
            "Text" => "{ get { return __native.Text; } set { var next = value ?? string.Empty; if (__native.Text == next) return; __native.Text = next; __NativeChanged(NativeChange.Text); } }",
            "Visible" => "{ get { return this is Form projectedForm && projectedForm.__HasHostedProjection ? projectedForm.__HostedProjectionVisible : __native.Visible; } set { var changed = Visible != value; if (!changed) return; if (value && this is Form form && __parent is null && Application.__CurrentForm is Form current && !global::System.Object.ReferenceEquals(current, form)) { form.__ShowNonModal(); return; } if (value) __RaiseLoad(); __native.Visible = value; if (this is Form projectedForm) projectedForm.__SetHostedProjectionVisible(value); __NativeChanged(NativeChange.Visible); __parent?.PerformLayout(); __parent?.__RefreshAutoSizeFromChildren(); __SynchronizeWindowSurfaceTree(); __TouchWindowSurface(); if (value && __loadRaised) __QueueManagedPaintTree(); } }",
            "Enabled" => "{ get { return __native.Enabled; } set { if (__PostCrossThreadMutation(() => Enabled = value)) return; if (__native.Enabled == value) return; __native.Enabled = value; __NativeChanged(NativeChange.Enabled); } }",
            "Bounds" => "{ get { return __native.Bounds; } set { var normalized = new global::System.Drawing.Rectangle(value.X, value.Y, global::System.Math.Max(0, value.Width), global::System.Math.Max(0, value.Height)); if (!__assigningLayoutBounds) __preferredSize = normalized.Size; var previous = __native.Bounds; if (previous == normalized) return; __native.Bounds = normalized; if (previous.Location != normalized.Location) { Move?.Invoke(this, global::System.EventArgs.Empty); LocationChanged?.Invoke(this, global::System.EventArgs.Empty); } if (previous.Size != normalized.Size) { __InvalidateManagedPaintSurface(); if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_RESIZE\") == \"1\") global::System.Console.Error.WriteLine(\"facade-resize=type:\" + GetType().FullName + \"|from:\" + previous.Width + \"x\" + previous.Height + \"|to:\" + normalized.Width + \"x\" + normalized.Height + \"|load:\" + __loadRaised); OnSizeChanged(global::System.EventArgs.Empty); OnResize(global::System.EventArgs.Empty); ClientSizeChanged?.Invoke(this, global::System.EventArgs.Empty); if (__parent is not null || __loadRaised) PerformLayout(); if (!__assigningLayoutBounds) __parent?.__RefreshAutoSizeFromChildren(); if (__loadRaised) __RenderManagedPaint(); } __SynchronizeWindowSurfaceTree(); } }",
            "Capture" => "{ get { return __native.Capture; } set { __native.Capture = value; } }",
            "CheckForIllegalCrossThreadCalls" => "{ set { __checkForIllegalCrossThreadCalls = value; } }",
            "ClientRectangle" => "{ get { return new global::System.Drawing.Rectangle(0, 0, Width, Height); } }",
            "ClientSize" => "{ get { return Size; } set { Size = value; } }",
            "ContainsFocus" => "{ get { return global::System.Object.ReferenceEquals(__focusedControl, this) || (__focusedControl is not null && __ContainsDescendant(__focusedControl)); } }",
            "ContextMenuStrip" => "{ get { return __contextMenuStrip!; } set { __contextMenuStrip = value; } }",
            "Controls" => "{ get { return __controls; } }",
            "Cursor" => "{ get { return __cursor!; } set { if (global::System.Object.ReferenceEquals(__cursor, value)) return; __cursor = value; __native.CursorKind = value?.__Kind ?? 0u; } }",
            "DefaultFont" => "{ get { return global::System.Drawing.SystemFonts.DefaultFont; } }",
            "DisplayRectangle" => "{ get { return ClientRectangle; } }",
            "Disposing" => "{ get { return __native.IsDisposed; } }",
            "Dock" => "{ get { return __dock; } set { if (__dock == value) return; __dock = value; __parent?.PerformLayout(); } }",
            "DoubleBuffered" => "{ get { return __DoubleBufferedRequested; } set { __SetDoubleBuffered(value); } }",
            "Focused" => "{ get { return global::System.Object.ReferenceEquals(__focusedControl, this); } }",
            "Font" => "{ get { return __font ?? global::System.Drawing.SystemFonts.DefaultFont; } set { if (global::System.Object.ReferenceEquals(__font, value)) return; __font = value; OnFontChanged(global::System.EventArgs.Empty); if (__loadRaised) __QueueManagedPaint(); } }",
            "ForeColor" => "{ get { return !__foreColor.IsEmpty ? __foreColor : __parent is not null ? __parent.ForeColor : global::System.Drawing.Color.FromArgb(27, 39, 51); } set { if (__foreColor == value) return; __foreColor = value; __ApplyEffectiveColors(); OnForeColorChanged(global::System.EventArgs.Empty); if (__loadRaised) __QueueManagedPaint(); } }",
            "Handle" => "{ get { return __AcquireCompatibilityHandle(); } }",
            "Height" => "{ get { return Bounds.Height; } set { var bounds = Bounds; bounds.Height = value; Bounds = bounds; } }",
            "InvokeRequired" => "{ get { return __native.InvokeRequired; } }",
            "IsDisposed" => "{ get { return __native.IsDisposed; } }",
            "IsHandleCreated" => "{ get { return __native.HasWindowHandle; } }",
            "Left" => "{ get { return Bounds.Left; } set { var bounds = Bounds; bounds.X = value; Bounds = bounds; } }",
            "Location" => "{ get { return Bounds.Location; } set { var bounds = Bounds; bounds.Location = value; Bounds = bounds; } }",
            "Margin" => "{ get { return __margin; } set { __margin = value; __parent?.PerformLayout(); } }",
            "MaximumSize" => "{ get { return __maximumSize; } set { if (value.Width < 0 || value.Height < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __maximumSize = value; } }",
            "MinimumSize" => "{ get { return __minimumSize; } set { if (value.Width < 0 || value.Height < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __minimumSize = value; } }",
            "ModifierKeys" => "{ get { return Keys.None; } }",
            "MousePosition" => "{ get { return __mousePosition; } }",
            "Padding" => "{ get { return __padding; } set { __padding = value; PerformLayout(); } }",
            "Parent" => "{ get { return __parent!; } set { __SetParent(value); } }",
            "Region" => "{ get { return __region!; } set { __region = value; } }",
            "PreferredSize" => "{ get { return GetPreferredSize(global::System.Drawing.Size.Empty); } }",
            "Right" => "{ get { return Bounds.Right; } }",
            "RightToLeft" => "{ get { return __rightToLeft; } set { __rightToLeft = value; } }",
            "Size" => "{ get { return Bounds.Size; } set { var bounds = Bounds; bounds.Size = value; Bounds = bounds; } }",
            "TabIndex" => "{ get { return __tabIndex; } set { __tabIndex = value; } }",
            "TabStop" => "{ get { return __tabStop; } set { __tabStop = value; } }",
            "Tag" => "{ get { return __tag!; } set { __tag = value; } }",
            "Top" => "{ get { return Bounds.Top; } set { var bounds = Bounds; bounds.Y = value; Bounds = bounds; } }",
            "Width" => "{ get { return Bounds.Width; } set { var bounds = Bounds; bounds.Width = value; Bounds = bounds; } }",
            _ => Stub(),
        };
    }
    if (type.FullName == "System.Windows.Forms.TextBoxBase")
        return property.Name switch
        {
            "BorderStyle" => "{ get { return __textBorderStyle; } set { __textBorderStyle = value; } }",
            "Multiline" => "{ get { return __textMultiline; } set { __textMultiline = value; } }",
            "ReadOnly" => "{ get { return __textReadOnly; } set { __textReadOnly = value; } }",
            "SelectedText" => "{ get { return Text.Substring(__TextSelectionStart, __TextSelectionLength); } set { if (__textReadOnly) return; __ReplaceTextSelection(value ?? string.Empty); } }",
            "SelectionLength" => "{ get { return __TextSelectionLength; } set { __SetTextSelection(__TextSelectionStart, value); } }",
            "SelectionStart" => "{ get { return __TextSelectionStart; } set { __SetTextSelection(value, 0); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Control+ControlCollection" && property.Name == "Item")
        return "{ get { return __items[index]; } }";
    if (type.FullName == "System.Windows.Forms.ScrollProperties")
        return property.Name switch
        {
            "Enabled" => "{ get { return __scrollEnabled; } set { if (__AutoOwned || __scrollEnabled == value) return; __scrollEnabled = value; __Commit(); } }",
            "LargeChange" => "{ get { return global::System.Math.Min(__scrollLargeChange, global::System.Math.Max(0, __scrollMaximum - __scrollMinimum + 1)); } set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); if (__scrollLargeChange == value) return; __scrollLargeChange = value; __Commit(); } }",
            "Maximum" => "{ get { return __scrollMaximum; } set { if (__AutoOwned || __scrollMaximum == value) return; __scrollMaximum = value; if (__scrollMinimum > value) __scrollMinimum = value; __scrollValue = global::System.Math.Clamp(__scrollValue, __scrollMinimum, __scrollMaximum); __Commit(); } }",
            "Minimum" => "{ get { return __scrollMinimum; } set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); if (__AutoOwned || __scrollMinimum == value) return; __scrollMinimum = value; if (__scrollMaximum < value) __scrollMaximum = value; __scrollValue = global::System.Math.Clamp(__scrollValue, __scrollMinimum, __scrollMaximum); __Commit(); } }",
            "ParentControl" => "{ get { return __scrollOwner!; } }",
            "SmallChange" => "{ get { return global::System.Math.Min(__scrollSmallChange, LargeChange); } set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); if (__scrollSmallChange == value) return; __scrollSmallChange = value; __Commit(); } }",
            "Value" => "{ get { return __scrollValue; } set { if (value < __scrollMinimum || value > __scrollMaximum) throw new global::System.ArgumentOutOfRangeException(nameof(value)); if (__scrollValue == value) return; __scrollValue = value; __Commit(); } }",
            "Visible" => "{ get { return __scrollVisible; } set { if (__AutoOwned || __scrollVisible == value) return; __scrollVisible = value; __Commit(); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ScrollEventArgs")
        return property.Name switch
        {
            "Type" => "{ get { return __scrollEventType; } }",
            "OldValue" => "{ get { return __scrollOldValue; } }",
            "NewValue" => "{ get { return __scrollNewValue; } set { __scrollNewValue = value; } }",
            "ScrollOrientation" => "{ get { return __scrollOrientation; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ScrollableControl")
        return property.Name switch
        {
            "AutoScroll" => "{ get { return __autoScroll; } set { __SetAutoScroll(value); } }",
            "AutoScrollMargin" => "{ get { return __RefreshScrollState().Margin; } set { __SetAutoScrollMargin(value, false); } }",
            "AutoScrollMinSize" => "{ get { return __RefreshScrollState().MinimumContentSize; } set { __SetAutoScrollMinSize(value); } }",
            "AutoScrollPosition" => "{ get { var value = __RefreshScrollState().Position; return new global::System.Drawing.Point(-value.X, -value.Y); } set { __SetAutoScrollPosition(value); } }",
            "DisplayRectangle" => "{ get { return __RefreshScrollState().DisplayRectangle; } }",
            "DockPadding" => "{ get { return __dockPadding; } }",
            "HScroll" => "{ get { __RefreshScrollState(); return __hScroll; } set { if (__autoScroll || __hScroll == value) return; __hScroll = value; __horizontalScroll.Visible = value; } }",
            "HorizontalScroll" => "{ get { __RefreshScrollState(); return __horizontalScroll; } }",
            "VScroll" => "{ get { __RefreshScrollState(); return __vScroll; } set { if (__autoScroll || __vScroll == value) return; __vScroll = value; __verticalScroll.Visible = value; } }",
            "VerticalScroll" => "{ get { __RefreshScrollState(); return __verticalScroll; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ScrollableControl+DockPaddingEdges")
        return property.Name switch
        {
            "All" => "{ set { __Set(value, value, value, value); } }",
            "Bottom" => "{ get { return __bottom; } set { __Set(__left, __top, __right, value); } }",
            "Left" => "{ get { return __left; } set { __Set(value, __top, __right, __bottom); } }",
            "Right" => "{ get { return __right; } set { __Set(__left, __top, value, __bottom); } }",
            "Top" => "{ get { return __top; } set { __Set(__left, value, __right, __bottom); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Cursors")
        return property.Name switch
        {
            "Cross" => "{ get { return __cross; } }",
            "Default" => "{ get { return __default; } }",
            "HSplit" => "{ get { return __hSplit; } }",
            "Hand" => "{ get { return __hand; } }",
            "No" => "{ get { return __no; } }",
            "SizeWE" => "{ get { return __sizeWE; } }",
            "VSplit" => "{ get { return __vSplit; } }",
            "WaitCursor" => "{ get { return __wait; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ContainerControl")
        return property.Name switch
        {
            "ActiveControl" => "{ get { var focused = Control.__FocusedControl; return focused is not null && (global::System.Object.ReferenceEquals(focused, this) || __ContainsDescendant(focused)) ? focused : null!; } set { if (value is null) { Control.__ClearFocusWithin(this); return; } if (!global::System.Object.ReferenceEquals(value, this) && !__ContainsDescendant(value)) throw new global::System.ArgumentException(\"ActiveControl must be a descendant of this container.\", nameof(value)); value.Focus(); } }",
            "AutoScaleDimensions" => "{ set { __autoScaleDimensions = value; } }",
            "AutoScaleMode" => "{ set { __autoScaleMode = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DataGridView")
        return property.Name switch
        {
            "AllowUserToAddRows" => "{ set { __allowUserToAddRows = value; } }",
            "AllowUserToDeleteRows" => "{ set { __allowUserToDeleteRows = value; } }",
            "AllowUserToResizeColumns" => "{ set { __allowUserToResizeColumns = value; } }",
            "AllowUserToResizeRows" => "{ set { __allowUserToResizeRows = value; } }",
            "AlternatingRowsDefaultCellStyle" => "{ get { return __alternatingRowsDefaultCellStyle; } }",
            "AutoGenerateColumns" => "{ set { __autoGenerateColumns = value; } }",
            "BackgroundColor" => "{ set { __gridBackgroundColor = value; BackColor = value; } }",
            "BorderStyle" => "{ set { __gridBorderStyle = value; } }",
            "ColumnHeadersBorderStyle" => "{ set { __columnHeaderBorderStyle = value; } }",
            "ColumnHeadersDefaultCellStyle" => "{ get { return __columnHeadersDefaultCellStyle; } }",
            "ColumnHeadersHeight" => "{ set { if (value < 4) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __columnHeadersHeight = value; } }",
            "ColumnHeadersHeightSizeMode" => "{ set { __columnHeadersHeightSizeMode = value; } }",
            "Columns" => "{ get { return __gridColumns; } }",
            "DataSource" => "{ set { __gridDataSource = value; } }",
            "DefaultCellStyle" => "{ get { return __defaultCellStyle; } }",
            "EditingControl" => "{ get { return null!; } }",
            "EnableHeadersVisualStyles" => "{ set { __enableHeadersVisualStyles = value; } }",
            "FirstDisplayedScrollingRowIndex" => "{ set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __firstDisplayedRow = value; } }",
            "GridColor" => "{ set { __gridColor = value; } }",
            "Item" => "{ get { return __Cell(columnIndex, rowIndex); } }",
            "MultiSelect" => "{ set { __multiSelect = value; } }",
            "ReadOnly" => "{ set { __gridReadOnly = value; } }",
            "RowHeadersBorderStyle" => "{ set { __rowHeaderBorderStyle = value; } }",
            "RowHeadersVisible" => "{ set { __rowHeadersVisible = value; } }",
            "RowTemplate" => "{ get { return __rowTemplate; } }",
            "Rows" => "{ get { return __gridRows; } }",
            "ScrollBars" => "{ set { __gridScrollBars = value; } }",
            "SelectedCells" => "{ get { return __selectedCells; } }",
            "SelectedRows" => "{ get { return __selectedRows; } }",
            "SelectionMode" => "{ set { __gridSelectionMode = value; } }",
            "ShowCellErrors" => "{ set { __showCellErrors = value; } }",
            "ShowCellToolTips" => "{ set { __showCellToolTips = value; } }",
            "ShowEditingIcon" => "{ set { __showEditingIcon = value; } }",
            "ShowRowErrors" => "{ set { __showRowErrors = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DataGridViewElement" && property.Name == "DataGridView")
        return "{ get { return __dataGridView!; } }";
    if (type.FullName == "System.Windows.Forms.DataGridViewBand")
        return property.Name switch
        {
            "DefaultCellStyle" => "{ get { return __bandDefaultCellStyle; } set { __bandDefaultCellStyle = value ?? throw new global::System.ArgumentNullException(nameof(value)); } }",
            "Displayed" => "{ get { return true; } }",
            "ReadOnly" => "{ set { __bandReadOnly = value; } }",
            "Resizable" => "{ set { __bandResizable = value; } }",
            "Selected" => "{ set { __bandSelected = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DataGridViewCell")
        return property.Name switch
        {
            "RowIndex" => "{ get { return __rowIndex; } }",
            "Style" => "{ get { return __cellStyle; } }",
            "Value" => "{ get { return __cellValue!; } }",
            "DefaultNewRowValue" => "{ get { return null!; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DataGridViewCellStyle")
        return property.Name switch
        {
            "Alignment" => "{ set { __alignment = value; } }",
            "BackColor" => "{ get { return __styleBackColor; } set { __styleBackColor = value; } }",
            "Font" => "{ get { return __styleFont!; } }",
            "ForeColor" => "{ get { return __styleForeColor; } set { __styleForeColor = value; } }",
            "Format" => "{ set { __format = value ?? string.Empty; } }",
            "SelectionBackColor" => "{ set { __selectionBackColor = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DataGridViewCellFormattingEventArgs")
        return property.Name switch
        {
            "ColumnIndex" => "{ get { return __formattingColumnIndex; } }",
            "FormattingApplied" => "{ set { __formattingApplied = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DataGridViewColumn")
        return property.Name switch
        {
            "CellTemplate" => "{ get { return __cellTemplate!; } set { __cellTemplate = value; } }",
            "AutoSizeMode" => "{ set { __columnAutoSizeMode = value; } }",
            "DataPropertyName" => "{ get { return __dataPropertyName; } set { __dataPropertyName = value ?? string.Empty; } }",
            "HeaderText" => "{ set { __headerText = value ?? string.Empty; } }",
            "FillWeight" => "{ set { if (value <= 0f) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __fillWeight = value; } }",
            "Name" => "{ set { __columnName = value ?? string.Empty; } }",
            "Width" => "{ set { __columnWidth = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DataGridViewRow" && property.Name == "Height")
        return "{ set { if (value < 2) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __rowHeight = value; } }";
    if (type.FullName == "System.Windows.Forms.DataGridViewColumnCollection" && property.Name == "Item")
        return "{ get { return __items[index]; } }";
    if (type.FullName == "System.Windows.Forms.DataGridViewRowCollection")
        return property.Name switch
        {
            "Count" => "{ get { __EnsureRows(); return __rows.Count; } }",
            "Item" => "{ get { __EnsureRows(); return __rows[index]; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.DataGridViewSelectedCellCollection" && property.Name == "Item")
        return "{ get { throw new global::System.ArgumentOutOfRangeException(nameof(index)); } }";
    if (type.FullName == "System.Windows.Forms.TableLayoutPanel")
        return property.Name switch
        {
            "ColumnCount" => "{ get { return __columnCount; } set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __columnCount = value; PerformLayout(); } }",
            "ColumnStyles" => "{ get { return __columnStyles; } }",
            "Controls" => "{ get { return __tableControls; } }",
            "RowCount" => "{ get { return __rowCount; } set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __rowCount = value; PerformLayout(); } }",
            "RowStyles" => "{ get { return __rowStyles; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.FlowLayoutPanel" && property.Name == "WrapContents")
        return "{ set { __wrapContents = value; PerformLayout(); } }";
    if (type.FullName == "System.Windows.Forms.TableLayoutColumnStyleCollection" && property.Name == "Item")
        return "{ set { if (value is null) throw new global::System.ArgumentNullException(nameof(value)); __styles[index] = value; } }";
    if (type.FullName == "System.Windows.Forms.CheckBox")
        return property.Name switch
        {
            "Checked" => "{ get { return __checkState != CheckState.Unchecked; } set { CheckState = value ? CheckState.Checked : CheckState.Unchecked; } }",
            "CheckState" => "{ set { if (__checkState == value) return; var oldChecked = __checkState != CheckState.Unchecked; __checkState = value; __NativeCheckState = (uint)value; CheckStateChanged?.Invoke(this, global::System.EventArgs.Empty); if (oldChecked != (__checkState != CheckState.Unchecked)) CheckedChanged?.Invoke(this, global::System.EventArgs.Empty); } }",
            "Appearance" => "{ set { __checkAppearance = value; Invalidate(); } }",
            "CheckAlign" => "{ set { __checkAlignment = value; Invalidate(); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ButtonBase")
        return property.Name switch
        {
            "FlatAppearance" => "{ get { return __flatAppearance; } }",
            "FlatStyle" => "{ set { if (!global::System.Enum.IsDefined(value)) throw new global::System.ComponentModel.InvalidEnumArgumentException(nameof(value), (int)value, typeof(FlatStyle)); if (__buttonFlatStyle == value) return; __buttonFlatStyle = value; __ApplyNativeButtonAppearance(); } }",
            "TextAlign" => "{ set { var compact = __CompactContentAlignment(value); __buttonTextAlign = value; if (__native.SupportsRaster) Invalidate(); else __native.SetTextAlignment(compact); } }",
            "UseVisualStyleBackColor" => "{ set { __useVisualStyleBackColor = value; Invalidate(); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Button" && property.Name == "DialogResult")
        return "{ get { return __buttonDialogResult; } set { if (!global::System.Enum.IsDefined(value)) throw new global::System.ComponentModel.InvalidEnumArgumentException(nameof(value), (int)value, typeof(DialogResult)); __buttonDialogResult = value; } }";
    if (type.FullName == "System.Windows.Forms.RadioButton")
        return property.Name switch
        {
            "Checked" => "{ get { return __checked; } set { if (__checked == value) return; __checked = value; if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=radio-native-write-begin|name=\" + Name + \"|checked=\" + value); __NativeCheckState = value ? 1u : 0u; if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=radio-native-write-end|name=\" + Name); __RaiseCheckedChanged(); } }",
            "TabStop" => "{ set { __radioTabStop = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ListControl")
        return property.Name switch
        {
            "DisplayMember" => "{ set { __displayMember = value ?? string.Empty; } }",
            "FormattingEnabled" => "{ set { __formattingEnabled = value; } }",
            "SelectedIndex" => "{ get { if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=list-selection-get|name=\" + Name + \"|value=\" + __selectedIndex + \"|count=\" + __SelectionItemCount); return __selectedIndex; } set { if (value < -1 || value >= __SelectionItemCount) throw new global::System.ArgumentOutOfRangeException(nameof(value)); if (__selectedIndex == value) return; var previous = __selectedIndex; __selectedIndex = value; if (__TraceInteraction) global::System.Console.Error.WriteLine(\"facade-interaction=list-selection-set|name=\" + Name + \"|previous=\" + previous + \"|value=\" + value + \"|count=\" + __SelectionItemCount); __OnSelectedIndexChanged(); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ListBox")
        return property.Name switch
        {
            "Items" => "{ get { return __listItems; } }",
            "DrawMode" => "{ set { __listDrawMode = value; Invalidate(); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ListBox+ObjectCollection")
        return property.Name switch
        {
            "Count" => "{ get { return __items.Count; } }",
            "Item" => "{ get { return __items[index]; } set { __items[index] = value ?? throw new global::System.ArgumentNullException(nameof(value)); __owner?.Invalidate(); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ProgressBar")
        return property.Name switch
        {
            "Maximum" => "{ get { return __progressMaximum; } }",
            "Minimum" => "{ get { return __progressMinimum; } }",
            "Value" => "{ set { if (value < __progressMinimum || value > __progressMaximum) throw new global::System.ArgumentOutOfRangeException(nameof(value)); if (__progressValue == value) return; __NativeRangeValue = value; __progressValue = value; Invalidate(); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.SplitContainer")
        return property.Name switch
        {
            "Panel1" => "{ get { return __panel1; } }",
            "Panel2" => "{ get { return __panel2; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Splitter")
        return property.Name switch
        {
            "SplitPosition" => "{ get { return __splitPosition; } set { __splitPosition = global::System.Math.Max(0, value); } }",
            "TabStop" => "{ set { __splitterTabStop = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ComboBox")
        return property.Name switch
        {
            "DropDownStyle" => "{ get { return __dropDownStyle; } set { __dropDownStyle = value; __NormalizeComboSelection(); __SetNativeFieldEditState(__comboAnchor, __comboCaret, value != ComboBoxStyle.DropDownList && Focused); Cursor = value == ComboBoxStyle.DropDownList ? Cursors.Default : null!; } }",
            "FlatStyle" => "{ get { return __comboFlatStyle; } set { __comboFlatStyle = value; } }",
            "AutoCompleteMode" => "{ set { __autoCompleteMode = value; } }",
            "AutoCompleteSource" => "{ set { __autoCompleteSource = value; } }",
            "DrawMode" => "{ set { __comboDrawMode = value; Invalidate(); } }",
            "DropDownHeight" => "{ set { if (value < 1) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __dropDownHeight = value; } }",
            "DropDownWidth" => "{ set { if (value < 1) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __dropDownWidth = value; } }",
            "IntegralHeight" => "{ set { __integralHeight = value; } }",
            "Items" => "{ get { return __comboItems; } }",
            "SelectedText" => "{ get { return Text.Substring(__ComboSelectionStart, __ComboSelectionLength); } set { if (__dropDownStyle == ComboBoxStyle.DropDownList) return; __ReplaceComboSelection(value ?? string.Empty); } }",
            "SelectionLength" => "{ get { return __ComboSelectionLength; } set { __SetComboSelection(__ComboSelectionStart, value); } }",
            "SelectionStart" => "{ get { return __ComboSelectionStart; } set { __SetComboSelection(value, 0); } }",
            "SelectedItem" => "{ get { return SelectedIndex >= 0 && SelectedIndex < __comboItems.Count ? __comboItems[SelectedIndex] : null!; } set { __selectedItem = value; SelectedIndex = value is null ? -1 : __comboItems.IndexOf(value); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ComboBox+ObjectCollection")
        return property.Name switch
        {
            "Count" => "{ get { return __items.Count; } }",
            "Item" => "{ get { return __items[index]; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.NumericUpDown")
        return property.Name switch
        {
            "DecimalPlaces" => "{ set { if (value < 0 || value > 99) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __decimalPlaces = value; __UpdateNumericText(); } }",
            "Increment" => "{ get { return __increment; } set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __increment = value; } }",
            "Maximum" => "{ get { return __maximum; } set { __maximum = value; if (__minimum > value) __minimum = value; if (__value > value) Value = value; } }",
            "Minimum" => "{ get { return __minimum; } set { __minimum = value; if (__maximum < value) __maximum = value; if (__value < value) Value = value; } }",
            "ThousandsSeparator" => "{ set { __thousandsSeparator = value; __UpdateNumericText(); } }",
            "Value" => "{ get { return __value; } set { if (value < __minimum || value > __maximum) throw new global::System.ArgumentOutOfRangeException(nameof(value)); if (__value == value) { __UpdateNumericText(); return; } __value = value; __UpdateNumericText(); ValueChanged?.Invoke(this, global::System.EventArgs.Empty); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Timer")
        return property.Name switch
        {
            "Enabled" => "{ get { return __timerEnabled; } set { if (__timerEnabled == value) return; __timerEnabled = value; __Schedule(); } }",
            "Interval" => "{ get { return __interval; } set { if (value < 1) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __interval = value; if (__timerEnabled) __Schedule(); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStrip")
        return property.Name switch
        {
            "GripMargin" => "{ get { return __gripMargin; } set { __gripMargin = value; } }",
            "Items" => "{ get { return __toolItems; } }",
            "Orientation" => "{ get { return Orientation.Horizontal; } }",
            "RenderMode" => "{ get { return __renderMode; } set { __renderMode = value; } }",
            "Renderer" => "{ get { return __renderer!; } set { __renderer = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripItem")
        return property.Name switch
        {
            "ContentRectangle" => "{ get { return new global::System.Drawing.Rectangle(0, 0, __itemSize.Width, __itemSize.Height); } }",
            "Enabled" => "{ get { return __itemEnabled; } }",
            "Height" => "{ get { return __itemSize.Height; } }",
            "Image" => "{ set { __itemImage = value; __RefreshOwner(); } }",
            "Margin" => "{ set { __itemMargin = value; __RefreshOwner(); } }",
            "Name" => "{ set { __itemName = value ?? string.Empty; } }",
            "Owner" => "{ get { return __owner!; } }",
            "Pressed" => "{ get { return false; } }",
            "Selected" => "{ get { return false; } }",
            "Size" => "{ set { __itemSize = value; __RefreshOwner(); } }",
            "Tag" => "{ get { return __itemTag!; } set { __itemTag = value; } }",
            "Text" => "{ get { return __itemText; } set { __itemText = value ?? string.Empty; __RefreshOwner(); } }",
            "ToolTipText" => "{ get { return __toolTipText; } set { __toolTipText = value ?? string.Empty; } }",
            "Visible" => "{ set { __itemVisible = value; __RefreshOwner(); } }",
            "Width" => "{ get { return __itemSize.Width; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripItemCollection" && property.Name == "Item")
        return "{ get { return __items[index]; } }";
    if (type.FullName == "System.Windows.Forms.ToolStripMenuItem" && property.Name == "Checked")
        return "{ get { return __menuChecked; } set { __menuChecked = value; } }";
    if (type.FullName == "System.Windows.Forms.ToolStripDropDownItem")
        return property.Name switch
        {
            "DropDown" => "{ get { return __dropDown; } }",
            "DropDownItems" => "{ get { return __dropDown.Items; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripDropDown" && property.Name == "OwnerItem")
        return "{ get { return __ownerItem!; } set { __ownerItem = value; } }";
    if (type.FullName == "System.Windows.Forms.ToolStripProfessionalRenderer" && property.Name == "ColorTable")
        return "{ get { return __colorTable; } }";
    if (type.FullName == "System.Windows.Forms.Padding")
        return property.Name switch
        {
            "All" => "{ get { return __left == __top && __left == __right && __left == __bottom ? __left : -1; } }",
            "Bottom" => "{ get { return __bottom; } }",
            "Left" => "{ get { return __left; } }",
            "Right" => "{ get { return __right; } }",
            "Top" => "{ get { return __top; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.PaintEventArgs")
        return property.Name switch
        {
            "ClipRectangle" => "{ get { return __clipRectangle; } }",
            "Graphics" => "{ get { return __graphics ?? throw new global::System.InvalidOperationException(\"PaintEventArgs does not have a graphics context.\"); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.InvalidateEventArgs" && property.Name == "InvalidRect")
        return "{ get { return __invalidRect; } }";
    if (type.FullName == "System.Windows.Forms.MouseEventArgs")
        return property.Name switch
        {
            "Button" => "{ get { return __button; } }",
            "Delta" => "{ get { return __delta; } }",
            "Location" => "{ get { return new global::System.Drawing.Point(__x, __y); } }",
            "X" => "{ get { return __x; } }",
            "Y" => "{ get { return __y; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.KeyEventArgs")
        return property.Name switch
        {
            "Handled" => "{ set { __handled = value; } }",
            "KeyCode" => "{ get { return __keyData & Keys.KeyCode; } }",
            "Modifiers" => "{ get { return __keyData & Keys.Modifiers; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.FlatButtonAppearance" && property.Name == "BorderSize")
        return "{ set { if (value < 0) throw new global::System.ArgumentOutOfRangeException(nameof(value)); if (__flatBorderSize == value) return; __flatBorderSize = value; __owner?.__ApplyNativeButtonAppearance(); } }";
    if (type.FullName == "System.Windows.Forms.GroupBox")
        return property.Name switch
        {
            "FlatStyle" => "{ set { __groupFlatStyle = value; Invalidate(); } }",
            "TabStop" => "{ set { __groupTabStop = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.KeyPressEventArgs" && property.Name == "Handled")
        return "{ set { __keyPressHandled = value; } }";
    if (type.FullName == "System.Windows.Forms.HandledMouseEventArgs" && property.Name == "Handled")
        return "{ set { __mouseHandled = value; } }";
    if (type.FullName == "System.Windows.Forms.Panel")
        return property.Name switch
        {
            "AutoSizeMode" => "{ set { __panelAutoSizeMode = value; SetAutoSizeMode(value); } }",
            "BorderStyle" => "{ set { if (!global::System.Enum.IsDefined(value)) throw new global::System.ComponentModel.InvalidEnumArgumentException(nameof(value), (int)value, typeof(BorderStyle)); if (__panelBorderStyle == value) return; __panelBorderStyle = value; if (__native.SupportsRaster) Invalidate(); else __native.SetPanelBorderStyle((uint)value); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.StatusStrip")
        return property.Name switch
        {
            "TabStop" => "{ set { __statusTabStop = value; } }",
            "SizeGripBounds" => "{ get { return new global::System.Drawing.Rectangle(global::System.Math.Max(0, Width - 16), global::System.Math.Max(0, Height - 16), global::System.Math.Min(16, Width), global::System.Math.Min(16, Height)); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.TextBox" && property.Name == "TextAlign")
        return "{ set { __textBoxAlignment = value; } }";
    if (type.FullName == "System.Windows.Forms.UserControl" && property.Name == "AutoSizeMode")
        return "{ set { __userControlAutoSizeMode = value; SetAutoSizeMode(value); } }";
    if (type.FullName == "System.Windows.Forms.PictureBox")
        return property.Name switch
        {
            "BorderStyle" => "{ set { __pictureBorderStyle = value; Invalidate(); } }",
            "ErrorImage" => "{ set { __errorImage = value; Invalidate(); } }",
            "Image" => "{ set { if (global::System.Object.ReferenceEquals(__pictureImage, value)) return; __pictureImage = value; Parent?.PerformLayout(); Invalidate(); } }",
            "InitialImage" => "{ set { __initialImage = value; Invalidate(); } }",
            "SizeMode" => "{ set { if (__pictureSizeMode == value) return; __pictureSizeMode = value; Parent?.PerformLayout(); Invalidate(); } }",
            "TabIndex" => "{ set { base.TabIndex = value; } }",
            "TabStop" => "{ set { base.TabStop = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.UpDownBase")
        return property.Name switch
        {
            "BorderStyle" => "{ get { return __upDownBorderStyle; } set { __upDownBorderStyle = value; } }",
            "CreateParams" => "{ get { return new CreateParams(); } }",
            "TextAlign" => "{ set { __upDownTextAlign = value; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripButton" && property.Name == "Checked")
        return "{ get { return __toolButtonChecked; } }";
    if (type.FullName == "System.Windows.Forms.ToolStripManager")
        return property.Name switch
        {
            "RenderMode" => "{ get { return __managerRenderMode; } set { __managerRenderMode = value; } }",
            "Renderer" => "{ get { return __managerRenderer; } set { __managerRenderer = value ?? throw new global::System.ArgumentNullException(nameof(value)); __managerRenderMode = ToolStripManagerRenderMode.Custom; } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripProfessionalRenderer" && property.Name == "RoundedEdges")
        return "{ set { __roundedEdges = value; } }";
    if (type.FullName == "System.Windows.Forms.ProfessionalColorTable")
        return property.Name switch
        {
            "ButtonCheckedGradientBegin" => "{ get { return global::System.Drawing.Color.FromArgb(224, 236, 249); } }",
            "ButtonCheckedGradientMiddle" => "{ get { return global::System.Drawing.Color.FromArgb(191, 219, 248); } }",
            "ButtonCheckedGradientEnd" => "{ get { return global::System.Drawing.Color.FromArgb(152, 194, 233); } }",
            "ButtonPressedBorder" => "{ get { return global::System.Drawing.Color.FromArgb(61, 123, 173); } }",
            "ButtonPressedGradientBegin" => "{ get { return global::System.Drawing.Color.FromArgb(208, 228, 247); } }",
            "ButtonPressedGradientMiddle" => "{ get { return global::System.Drawing.Color.FromArgb(170, 204, 235); } }",
            "ButtonPressedGradientEnd" => "{ get { return global::System.Drawing.Color.FromArgb(126, 175, 216); } }",
            "ButtonSelectedBorder" => "{ get { return global::System.Drawing.Color.FromArgb(91, 143, 185); } }",
            "ButtonSelectedGradientBegin" => "{ get { return global::System.Drawing.Color.FromArgb(238, 246, 254); } }",
            "ButtonSelectedGradientMiddle" => "{ get { return global::System.Drawing.Color.FromArgb(213, 232, 250); } }",
            "ButtonSelectedGradientEnd" => "{ get { return global::System.Drawing.Color.FromArgb(175, 210, 241); } }",
            "MenuItemBorder" => "{ get { return global::System.Drawing.Color.FromArgb(112, 153, 187); } }",
            "MenuItemSelected" => "{ get { return global::System.Drawing.Color.FromArgb(217, 235, 252); } }",
            "MenuItemSelectedGradientBegin" => "{ get { return global::System.Drawing.Color.FromArgb(236, 246, 255); } }",
            "MenuItemSelectedGradientEnd" => "{ get { return global::System.Drawing.Color.FromArgb(190, 220, 246); } }",
            _ => Stub(),
        };
    if (type.FullName == "System.Windows.Forms.Form")
        return property.Name switch
        {
            "ClientSize" => "{ set { base.ClientSize = value; } }",
            "AcceptButton" => "{ get { return __acceptButton!; } set { if (global::System.Object.ReferenceEquals(__acceptButton, value)) return; __acceptButton?.NotifyDefault(false); __acceptButton = value; __acceptButton?.NotifyDefault(true); } }",
            "ActiveMdiChild" => "{ get { return null!; } }",
            "CancelButton" => "{ get { return __cancelButton!; } set { __cancelButton = value; } }",
            "ControlBox" => "{ get { return __controlBox; } set { __controlBox = value; } }",
            "CreateParams" => "{ get { return __createParams; } }",
            "DialogResult" => "{ get { return __dialogResult; } set { if (!global::System.Enum.IsDefined(value)) throw new global::System.ComponentModel.InvalidEnumArgumentException(nameof(value), (int)value, typeof(DialogResult)); __dialogResult = value; if (value != DialogResult.None && __modal && Visible) __RequestClose(); } }",
            "FormBorderStyle" => "{ set { __formBorderStyle = value; } }",
            "Icon" => "{ get { return __formIcon; } set { __formIcon = value ?? __CreateDefaultFormIcon(); } }",
            "KeyPreview" => "{ get { return __keyPreview; } set { __keyPreview = value; } }",
            "Location" => "{ get { return base.Location; } set { base.Location = value; } }",
            "MainMenuStrip" => "{ get { return __mainMenuStrip!; } }",
            "MaximizeBox" => "{ set { __maximizeBox = value; } }",
            "MinimizeBox" => "{ set { __minimizeBox = value; } }",
            "Opacity" => "{ set { if (value < 0d || value > 1d) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __opacity = value; } }",
            "Owner" => "{ get { return __ownerForm!; } set { __SetOwner(value); } }",
            "Margin" => "{ set { base.Margin = value; } }",
            "MdiParent" => "{ get { return __mdiParent!; } set { __mdiParent = value; } }",
            "RightToLeftLayout" => "{ get { return __rightToLeftLayout; } set { __rightToLeftLayout = value; } }",
            "ShowIcon" => "{ set { __showIcon = value; } }",
            "ShowInTaskbar" => "{ set { __showInTaskbar = value; } }",
            "Size" => "{ get { return base.Size; } set { base.Size = value; } }",
            "SizeGripStyle" => "{ set { __sizeGripStyle = value; } }",
            "StartPosition" => "{ set { __startPosition = value; } }",
            "TopLevel" => "{ get { return __topLevel; } set { __topLevel = value; } }",
            "TopMost" => "{ set { __topMost = value; } }",
            "WindowState" => "{ get { return __windowState; } set { __windowState = value; } }",
            _ => Stub(),
        };
    return Stub();
}

static string StubProperty(Type type, PropertyInfo property, bool isInterface, bool get, bool set) => "{ " +
    (get ? isInterface ? "get; " : $"get {{ return global::{(type.Assembly.GetName().Name == "System.Drawing.Common" ? "System.Drawing" : "System.Windows.Forms")}.FacadeStubDiagnostics.Value<{CsType(property.PropertyType)}>(\"{type.FullName}.{property.Name}\"); }} " : "") +
    (set ? isInterface ? "set; " : "set { } " : "") + "}";

static string DrawingImageBody(MethodInfo method)
{
    var parameters = method.GetParameters();
    if (parameters.Length == 2)
        return "{ __EnsureRecorder(); NativeDrawingBridge.DrawImage(__recorder, image, new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height), new global::System.Drawing.RectangleF(0, 0, image.Width, image.Height), null); }";
    if (parameters.Length == 3)
        return "{ __EnsureRecorder(); NativeDrawingBridge.DrawImage(__recorder, image, new global::System.Drawing.RectangleF(x, y, image.Width, image.Height), new global::System.Drawing.RectangleF(0, 0, image.Width, image.Height), null); }";
    if (parameters.Length == 5)
        return "{ __EnsureRecorder(); NativeDrawingBridge.DrawImage(__recorder, image, new global::System.Drawing.RectangleF(x, y, width, height), new global::System.Drawing.RectangleF(0, 0, image.Width, image.Height), null); }";
    if (parameters.Length == 4 && parameters[1].ParameterType.FullName == "System.Drawing.Rectangle")
        return "{ __EnsureRecorder(); NativeDrawingBridge.DrawImage(__recorder, image, new global::System.Drawing.RectangleF(destRect.X, destRect.Y, destRect.Width, destRect.Height), new global::System.Drawing.RectangleF(srcRect.X, srcRect.Y, srcRect.Width, srcRect.Height), null); }";
    var attributeName = parameters.Last().ParameterType.FullName == "System.Drawing.Imaging.ImageAttributes" ?
        parameters.Last().Name : "null";
    return "{ __EnsureRecorder(); NativeDrawingBridge.DrawImage(__recorder, image, new global::System.Drawing.RectangleF(destRect.X, destRect.Y, destRect.Width, destRect.Height), new global::System.Drawing.RectangleF(srcX, srcY, srcWidth, srcHeight), " + attributeName + "); }";
}

static string MethodBody(Type type, MethodInfo method)
{
    if (type.FullName == "System.Drawing.Design.UITypeEditor")
    {
        if (method.Name == "EditValue")
            return method.GetParameters().Length == 2
                ? "{ return EditValue(null, provider, value); }"
                : "{ return value; }";
        if (method.Name == "GetEditStyle")
            return method.GetParameters().Length == 0
                ? "{ return GetEditStyle(null); }"
                : "{ return global::System.Drawing.Design.UITypeEditorEditStyle.None; }";
        if (method.Name == "GetPaintValueSupported")
            return method.GetParameters().Length == 0
                ? "{ return GetPaintValueSupported(null); }"
                : "{ return false; }";
        if (method.Name == "PaintValue")
            return method.GetParameters().Length == 1
                ? "{ }"
                : "{ if (canvas is null) throw new global::System.ArgumentNullException(nameof(canvas)); PaintValue(new global::System.Drawing.Design.PaintValueEventArgs(null, value, canvas, rectangle)); }";
    }
    if (type.FullName == "System.Windows.Forms.PropertyGrid" &&
        method.Name == "Refresh")
        return "{ __native.RefreshPropertyGrid(); Invalidate(); Update(); }";
    if (type.FullName == "System.Windows.Forms.CommonDialog" && method.Name == "ShowDialog")
        return method.GetParameters().Length == 0
            ? "{ return __RunDialog(null); }"
            : "{ return __RunDialog(owner); }";
    if (type.FullName == "System.Windows.Forms.ToolTip")
        return method.Name switch
        {
            "GetToolTip" => "{ if (control is null) throw new global::System.ArgumentNullException(nameof(control)); return __toolTips.TryGetValue(control, out var value) ? value : string.Empty; }",
            "SetToolTip" => "{ if (control is null) throw new global::System.ArgumentNullException(nameof(control)); caption ??= string.Empty; if (caption.Length == 0) { __toolTips.Remove(control); if (__toolTipHandlers.Remove(control, out var handlers)) { control.MouseEnter -= handlers.Enter; control.MouseLeave -= handlers.Leave; } __CancelPending(control, true); return; } __toolTips[control] = caption; if (!__toolTipHandlers.ContainsKey(control)) { global::System.EventHandler enter = (_, _) => { if (__toolTips.TryGetValue(control, out var current)) __Schedule(control, current); }; global::System.EventHandler leave = (_, _) => { __CancelPending(control, true); if (global::System.Object.ReferenceEquals(__shownWindow, control)) { __shownWindow = null; __shownText = string.Empty; } }; __toolTipHandlers[control] = (enter, leave); control.MouseEnter += enter; control.MouseLeave += leave; } }",
            "Show" => "{ if (window is null) throw new global::System.ArgumentNullException(nameof(window)); if (duration < 0) throw new global::System.ArgumentOutOfRangeException(nameof(duration)); if (!__toolTipActive) return; __shownWindow = window; __shownText = text ?? string.Empty; if (window is Control control) { __CancelPending(control, true); if (__shownText.Length != 0) { __toolTips[control] = __shownText; control.__ShowToolTip(__shownText, x, y, duration); } } if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_TOOLTIPS\") == \"1\") global::System.Console.Error.WriteLine(\"facade-tooltip=show|text=\" + __shownText.Replace('\\r', ' ').Replace('\\n', ' ') + \"|x=\" + x + \"|y=\" + y + \"|duration=\" + duration + \"|provider=native\"); }",
            "Hide" => "{ if (win is null) throw new global::System.ArgumentNullException(nameof(win)); if (win is Control control) __CancelPending(control, true); if (global::System.Object.ReferenceEquals(__shownWindow, win)) { __shownWindow = null; __shownText = string.Empty; } }",
            _ => BodyFor(method.ReturnType, false),
        };
    if (type.FullName == "System.Windows.Forms.MessageBox" && method.Name == "Show")
    {
        var parameters = method.GetParameters();
        if (parameters.Length == 1)
            return "{ return __Show(text, string.Empty, MessageBoxButtons.OK, MessageBoxIcon.None, MessageBoxDefaultButton.Button1); }";
        if (parameters.Length == 2)
            return "{ return __Show(text, caption, MessageBoxButtons.OK, MessageBoxIcon.None, MessageBoxDefaultButton.Button1); }";
        if (parameters[0].ParameterType.FullName == "System.Windows.Forms.IWin32Window")
            return "{ return __Show(text, caption, buttons, icon, MessageBoxDefaultButton.Button1); }";
        if (parameters.Length == 4)
            return "{ return __Show(text, caption, buttons, icon, MessageBoxDefaultButton.Button1); }";
        return "{ return __Show(text, caption, buttons, icon, defaultButton); }";
    }
    if (type.FullName == "System.Windows.Forms.Clipboard")
    {
        if (method.Name == "GetText")
            return "{ var host = Application.__CurrentForm; if (host is not null) { __clipboardText = host.__ReadClipboardText(); } return __clipboardText; }";
        if (method.Name == "SetText")
            return "{ if (text is null) throw new global::System.ArgumentNullException(nameof(text)); __clipboardText = text; Application.__CurrentForm?.__WriteClipboardText(text); }";
    }
    if (type.FullName == "System.Windows.Forms.LinkLabel+LinkCollection" && method.Name == "Add")
        return "{ if (value is null) throw new global::System.ArgumentNullException(nameof(value)); __linkItems.Add(value); return __linkItems.Count - 1; }";
    if (type.FullName == "System.Windows.Forms.ContainerControl")
    {
        if (method.Name == "ProcessCmdKey") return "{ return FindForm()?.__ProcessDialogKey(keyData) ?? false; }";
        if (method.Name == "SelectNextControl")
            return "{ if (Controls.Count == 0) return false; var start = ctl is null ? (forward ? -1 : 0) : Controls.IndexOf(ctl); for (var offset = 1; offset <= Controls.Count; ++offset) { var index = forward ? start + offset : start - offset; if (wrap) index = (index % Controls.Count + Controls.Count) % Controls.Count; else if (index < 0 || index >= Controls.Count) return false; var candidate = Controls[index]; if ((!tabStopOnly || candidate.__TabStop) && candidate.Enabled && candidate.Visible && candidate.Focus()) return true; } return false; }";
    }
    if (type.FullName == "System.Windows.Forms.Control")
    {
        if (method.Name == "FromChildHandle") return "{ return null!; }";
        if (method.Name == "SelectNextControl")
            return "{ if (Controls.Count == 0) return false; var start = ctl is null ? (forward ? -1 : 0) : Controls.IndexOf(ctl); for (var offset = 1; offset <= Controls.Count; ++offset) { var index = forward ? start + offset : start - offset; if (wrap) index = (index % Controls.Count + Controls.Count) % Controls.Count; else if (index < 0 || index >= Controls.Count) return false; var candidate = Controls[index]; if ((!tabStopOnly || candidate.__TabStop) && candidate.Enabled && candidate.Visible && candidate.Focus()) return true; } return false; }";
    }
    if (type.FullName == "System.Windows.Forms.Layout.ArrangedElementCollection")
    {
        if (method.Name == "GetEnumerator") return "{ return global::System.Array.Empty<object>().GetEnumerator(); }";
    }
    if (type.FullName == "System.Windows.Forms.Screen" && method.Name == "GetWorkingArea")
        return "{ return new global::System.Drawing.Rectangle(0, 0, 1920, 1040); }";
    if (type.FullName == "System.Windows.Forms.ControlPaint")
    {
        var amount = method.GetParameters().Length == 2
            ? "global::System.Math.Clamp(" + method.GetParameters()[1].Name + ", 0f, 1f)"
            : "0.25f";
        if (method.Name == "Dark")
            return "{ var amount = " + amount + "; return global::System.Drawing.Color.FromArgb(baseColor.A, (int)(baseColor.R * (1f - amount)), (int)(baseColor.G * (1f - amount)), (int)(baseColor.B * (1f - amount))); }";
        if (method.Name == "Light" || method.Name == "LightLight")
        {
            var lightAmount = method.Name == "LightLight" ? "0.5f" : amount;
            return "{ var amount = " + lightAmount + "; return global::System.Drawing.Color.FromArgb(baseColor.A, baseColor.R + (int)((255 - baseColor.R) * amount), baseColor.G + (int)((255 - baseColor.G) * amount), baseColor.B + (int)((255 - baseColor.B) * amount)); }";
        }
    }
    if (type.FullName == "System.Windows.Forms.TextRenderer")
    {
        if (method.Name == "MeasureText" && method.GetParameters().Length == 2)
            return "{ if (text is null) throw new global::System.ArgumentNullException(nameof(text)); if (font is null) throw new global::System.ArgumentNullException(nameof(font)); if (text.Length == 0) return global::System.Drawing.Size.Empty; using var bitmap = new global::System.Drawing.Bitmap(1, 1); using var graphics = global::System.Drawing.Graphics.FromImage(bitmap); var measured = graphics.MeasureString(text, font); return new global::System.Drawing.Size(global::System.Math.Max(1, (int)global::System.Math.Ceiling(measured.Width) + 8), global::System.Math.Max(1, (int)global::System.Math.Ceiling(measured.Height) + 4)); }";
        if (method.Name == "MeasureText")
            return "{ if (text is null) throw new global::System.ArgumentNullException(nameof(text)); if (font is null) throw new global::System.ArgumentNullException(nameof(font)); if (text.Length == 0) return global::System.Drawing.Size.Empty; using var bitmap = new global::System.Drawing.Bitmap(1, 1); using var graphics = global::System.Drawing.Graphics.FromImage(bitmap); var widthLimit = proposedSize.Width > 0 && (flags & global::System.Windows.Forms.TextFormatFlags.WordBreak) != 0 ? proposedSize.Width : 0; using var format = new global::System.Drawing.StringFormat((flags & global::System.Windows.Forms.TextFormatFlags.SingleLine) != 0 ? global::System.Drawing.StringFormatFlags.NoWrap : (global::System.Drawing.StringFormatFlags)0); var measured = widthLimit > 0 ? graphics.MeasureString(text, font, widthLimit, format) : graphics.MeasureString(text, font); var width = (int)global::System.Math.Ceiling(measured.Width) + 8; var height = (int)global::System.Math.Ceiling(measured.Height) + 4; if (proposedSize.Width > 0) width = global::System.Math.Min(width, proposedSize.Width); if (proposedSize.Height > 0) height = global::System.Math.Min(height, proposedSize.Height); return new global::System.Drawing.Size(global::System.Math.Max(1, width), global::System.Math.Max(1, height)); }";
        if (method.Name == "DrawText")
            return "{ if (dc is null) throw new global::System.ArgumentNullException(nameof(dc)); if (text is null) throw new global::System.ArgumentNullException(nameof(text)); if (font is null) throw new global::System.ArgumentNullException(nameof(font)); if (dc is not global::System.Drawing.Graphics graphics || bounds.Width <= 0 || bounds.Height <= 0 || text.Length == 0) return; if (global::System.Environment.GetEnvironmentVariable(\"GUI_FORMS_TRACE_TEXT\") == \"1\") global::System.Console.Error.WriteLine(\"facade-text-draw=text:\" + text.Replace('\\r', ' ').Replace('\\n', ' ') + \"|bounds:\" + bounds.X + \",\" + bounds.Y + \",\" + bounds.Width + \",\" + bounds.Height + \"|font-height:\" + font.Height); var measured = graphics.MeasureString(text, font); var x = (float)bounds.X; var y = (float)bounds.Y; if ((flags & global::System.Windows.Forms.TextFormatFlags.HorizontalCenter) != 0) x += global::System.Math.Max(0f, (bounds.Width - measured.Width) / 2f); else if ((flags & global::System.Windows.Forms.TextFormatFlags.Right) != 0) x += global::System.Math.Max(0f, bounds.Width - measured.Width); if ((flags & global::System.Windows.Forms.TextFormatFlags.VerticalCenter) != 0) y += global::System.Math.Max(0f, (bounds.Height - measured.Height) / 2f); else if ((flags & global::System.Windows.Forms.TextFormatFlags.Bottom) != 0) y += global::System.Math.Max(0f, bounds.Height - measured.Height); using var brush = new global::System.Drawing.SolidBrush(foreColor); graphics.DrawString(text, font, brush, new global::System.Drawing.PointF(x, y)); }";
    }
    if (type.FullName == "System.Windows.Forms.ToolStripDropDown" && method.Name == "Show")
    {
        var parameters = method.GetParameters();
        if (parameters.Length == 1)
            return "{ __ShowDropDown(screenLocation); }";
        if (parameters.Length == 2 && parameters[0].ParameterType.FullName == "System.Drawing.Point")
            return "{ __ShowDropDown(position); }";
        if (parameters.Length == 2)
            return "{ if (control is null) throw new global::System.ArgumentNullException(nameof(control)); __ShowDropDown(control.PointToScreen(position), control); }";
        return "{ if (control is null) throw new global::System.ArgumentNullException(nameof(control)); __ShowDropDown(control.PointToScreen(new global::System.Drawing.Point(x, y)), control); }";
    }
    if (type.FullName == "System.Drawing.Icon")
    {
        if (method.Name == "ToBitmap") return "{ if (__iconDisposed) throw new global::System.ObjectDisposedException(nameof(Icon)); return NativeDrawingBridge.DecodeIcon(__iconData); }";
        if (method.Name == "Dispose") return "{ if (__iconDisposed) return; __iconDisposed = true; __iconData = global::System.Array.Empty<byte>(); global::System.GC.SuppressFinalize(this); }";
    }
    if (type.FullName == "System.Drawing.Brush" && method.Name == "Dispose")
        return "{ if (__brushDisposed) return; __brushDisposed = true; NativeDrawingBridge.Release(ref __handle); global::System.GC.SuppressFinalize(this); }";
    if (type.FullName == "System.Drawing.Pen" && method.Name == "Dispose")
        return "{ if (__penDisposed) return; __penDisposed = true; NativeDrawingBridge.Release(ref __handle); global::System.GC.SuppressFinalize(this); }";
    if (type.FullName == "System.Drawing.Font" && method.Name == "Dispose")
        return "{ if (__fontDisposed) return; __fontDisposed = true; NativeDrawingBridge.Release(ref __handle); global::System.GC.SuppressFinalize(this); }";
    if (type.FullName == "System.Drawing.StringFormat" && method.Name == "Dispose")
        return "{ if (__formatDisposed) return; __formatDisposed = true; NativeDrawingBridge.Release(ref __handle); global::System.GC.SuppressFinalize(this); }";
    if (type.FullName == "System.Drawing.Image")
    {
        if (method.Name == "Dispose") return "{ if (__imageDisposed) return; __FlushGraphics(); __imageDisposed = true; NativeDrawingBridge.Release(ref __bitmap); global::System.GC.SuppressFinalize(this); }";
        if (method.Name == "Save" && method.GetParameters().Length == 2) return "{ if (stream is null) throw new global::System.ArgumentNullException(nameof(stream)); __FlushGraphics(); var png = NativeDrawingBridge.EncodePng(__bitmap); stream.Write(png, 0, png.Length); }";
        if (method.Name == "Save") return "{ if (filename is null) throw new global::System.ArgumentNullException(nameof(filename)); __FlushGraphics(); global::System.IO.File.WriteAllBytes(filename, NativeDrawingBridge.EncodePng(__bitmap)); }";
        if (method.Name == "Clone") return "{ __FlushGraphics(); return NativeDrawingBridge.CloneBitmap(this); }";
        if (method.Name == "GetThumbnailImage") return "{ __FlushGraphics(); return NativeDrawingBridge.Thumbnail(this, thumbWidth, thumbHeight); }";
        if (method.Name == "FromStream") return "{ if (stream is null) throw new global::System.ArgumentNullException(nameof(stream)); using var copy = new global::System.IO.MemoryStream(); stream.CopyTo(copy); return NativeDrawingBridge.DecodePng(copy.ToArray()); }";
        if (method.Name == "FromFile") return "{ if (filename is null) throw new global::System.ArgumentNullException(nameof(filename)); return NativeDrawingBridge.DecodePng(global::System.IO.File.ReadAllBytes(filename)); }";
        if (method.Name == "FromHbitmap") return "{ return NativeDrawingBridge.FromHbitmap(hbitmap); }";
    }
    if (type.FullName == "System.Drawing.Bitmap")
    {
        if (method.Name == "GetPixel") return "{ __FlushGraphics(); return NativeDrawingBridge.BitmapGetPixel(__bitmap, x, y); }";
        if (method.Name == "SetPixel") return "{ __FlushGraphics(); NativeDrawingBridge.BitmapSetPixel(__bitmap, x, y, color); }";
        if (method.Name == "MakeTransparent") return "{ __FlushGraphics(); NativeDrawingBridge.BitmapMakeTransparent(__bitmap, transparentColor); }";
        if (method.Name == "GetHbitmap") return "{ __FlushGraphics(); return NativeDrawingBridge.GetHbitmap(this, background); }";
        if (method.Name == "LockBits") return "{ __FlushGraphics(); return NativeDrawingBridge.BitmapLock(__bitmap, rect, flags, format); }";
        if (method.Name == "UnlockBits") return "{ if (bitmapdata is null) throw new global::System.ArgumentNullException(nameof(bitmapdata)); NativeDrawingBridge.BitmapUnlock(__bitmap, bitmapdata); }";
    }
    if (type.FullName == "System.Drawing.Graphics")
    {
        var count = method.GetParameters().Length;
        if (method.Name == "FromImage") return "{ if (image is null) throw new global::System.ArgumentNullException(nameof(image)); if (image.__BitmapHandle.IsNull) throw new global::System.ArgumentException(\"Image has no native bitmap.\", nameof(image)); FacadeCallTelemetry.Observe(\"graphics.target-kind\", \"bitmap\"); FacadeCallTelemetry.Observe(\"graphics.target-dimensions\", image.Width.ToString(global::System.Globalization.CultureInfo.InvariantCulture) + \"x\" + image.Height.ToString(global::System.Globalization.CultureInfo.InvariantCulture)); var graphics = new global::System.Drawing.Graphics { __target = image }; image.__AttachGraphics(graphics); return graphics; }";
        if (method.Name is "FromHdc" or "FromHdcInternal") return "{ return NativeDrawingBridge.GraphicsFromNativeSurface(" + method.GetParameters()[0].Name + ", 0); }";
        if (method.Name == "FromHwnd") return "{ return NativeDrawingBridge.GraphicsFromNativeSurface(" + method.GetParameters()[0].Name + ", 1); }";
        if (method.Name == "Dispose") return "{ if (__graphicsDisposed) return; try { if (__hdcLeaseToken != 0) NativeDrawingBridge.ReleaseHdc(this, __leasedHdc); NativeDrawingBridge.Flush(this); } finally { __graphicsDisposed = true; var target = __target; target?.__DetachGraphics(this); NativeDrawingBridge.Release(ref __recorder); __target = null; if (__nativeSurface != 0) target?.Dispose(); __nativeSurface = 0; global::System.GC.SuppressFinalize(this); } }";
        if (method.Name == "Clear") return "{ __EnsureRecorder(); NativeDrawingBridge.RecorderClear(__recorder, color); }";
        if (method.Name == "FillRectangle")
        {
            var first = method.GetParameters()[1].ParameterType.FullName;
            if (first == "System.Drawing.Rectangle") return "{ __EnsureRecorder(); NativeDrawingBridge.FillRectangle(__recorder, brush.__handle, new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height)); }";
            if (first == "System.Drawing.RectangleF") return "{ __EnsureRecorder(); NativeDrawingBridge.FillRectangle(__recorder, brush.__handle, rect); }";
            return "{ __EnsureRecorder(); NativeDrawingBridge.FillRectangle(__recorder, brush.__handle, new global::System.Drawing.RectangleF(x, y, width, height)); }";
        }
        if (method.Name == "FillRectangles") return "{ if (rects is null) throw new global::System.ArgumentNullException(nameof(rects)); __EnsureRecorder(); foreach (var rect in rects) NativeDrawingBridge.FillRectangle(__recorder, brush.__handle, new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height)); }";
        if (method.Name == "DrawRectangle")
        {
            if (count == 2) return "{ __EnsureRecorder(); NativeDrawingBridge.DrawRectangle(__recorder, pen.__handle, new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height)); }";
            return "{ __EnsureRecorder(); NativeDrawingBridge.DrawRectangle(__recorder, pen.__handle, new global::System.Drawing.RectangleF(x, y, width, height)); }";
        }
        if (method.Name == "DrawLine")
        {
            if (count == 3) return "{ __DrawLine(pen, new global::System.Drawing.PointF(pt1.X, pt1.Y), new global::System.Drawing.PointF(pt2.X, pt2.Y)); }";
            return "{ __DrawLine(pen, new global::System.Drawing.PointF(x1, y1), new global::System.Drawing.PointF(x2, y2)); }";
        }
        if (method.Name == "DrawLines") return "{ if (points is null) throw new global::System.ArgumentNullException(nameof(points)); __EnsureRecorder(); for (var i = 1; i < points.Length; ++i) NativeDrawingBridge.DrawLine(__recorder, pen.__handle, new global::System.Drawing.PointF(points[i - 1].X, points[i - 1].Y), new global::System.Drawing.PointF(points[i].X, points[i].Y)); }";
        if (method.Name == "DrawEllipse") return "{ __EnsureRecorder(); NativeDrawingBridge.DrawEllipse(__recorder, pen.__handle, new global::System.Drawing.RectangleF(x, y, width, height)); }";
        if (method.Name == "FillEllipse") return "{ __EnsureRecorder(); NativeDrawingBridge.FillEllipse(__recorder, brush.__handle, new global::System.Drawing.RectangleF(x, y, width, height)); }";
        if (method.Name == "MeasureString") return "{ if (global::System.String.IsNullOrEmpty(text)) return global::System.Drawing.SizeF.Empty; if (font is null) throw new global::System.ArgumentNullException(nameof(font)); return NativeDrawingBridge.MeasureString(text, font, " + (count == 4 ? "format, global::System.Math.Max(0, width)" : "null, 0") + "); }";
        if (method.Name == "DrawString")
        {
            const string validate = "if (brush is null) throw new global::System.ArgumentNullException(nameof(brush)); if (global::System.String.IsNullOrEmpty(s)) return; if (font is null) throw new global::System.ArgumentNullException(nameof(font)); ";
            if (count == 4 && method.GetParameters()[3].ParameterType.FullName == "System.Drawing.PointF") return "{ " + validate + "__EnsureRecorder(); NativeDrawingBridge.DrawString(__recorder, s, font, brush, point, null); }";
            if (count == 5 && method.GetParameters()[3].ParameterType.FullName == "System.Drawing.PointF") return "{ " + validate + "__EnsureRecorder(); NativeDrawingBridge.DrawString(__recorder, s, font, brush, point, format); }";
            if (count >= 4 && method.GetParameters()[3].ParameterType.FullName == "System.Drawing.RectangleF")
            {
                var suppliedFormat = count == 5 ? "format" : "null";
                return "{ " + validate + "__EnsureRecorder(); global::System.Drawing.StringFormat? effectiveFormat = " + suppliedFormat + "; var origin = layoutRectangle.Location; if (effectiveFormat is not null) { var measured = NativeDrawingBridge.MeasureString(s, font, effectiveFormat, global::System.Math.Max(0, (int)layoutRectangle.Width)); if (effectiveFormat.__alignment == global::System.Drawing.StringAlignment.Center) origin.X += layoutRectangle.Width * 0.5f; else if (effectiveFormat.__alignment == global::System.Drawing.StringAlignment.Far) origin.X += layoutRectangle.Width; if (effectiveFormat.__lineAlignment == global::System.Drawing.StringAlignment.Center) origin.Y += global::System.Math.Max(0f, (layoutRectangle.Height - measured.Height) * 0.5f); else if (effectiveFormat.__lineAlignment == global::System.Drawing.StringAlignment.Far) origin.Y += global::System.Math.Max(0f, layoutRectangle.Height - measured.Height); } NativeDrawingBridge.DrawString(__recorder, s, font, brush, origin, effectiveFormat); }";
            }
            return "{ " + validate + "__EnsureRecorder(); NativeDrawingBridge.DrawString(__recorder, s, font, brush, new global::System.Drawing.PointF(x, y), null); }";
        }
        if (method.Name == "DrawImage" || method.Name == "DrawImageUnscaled") return DrawingImageBody(method);
        if (method.Name == "DrawPath") return "{ if (path is null) throw new global::System.ArgumentNullException(nameof(path)); if (FacadeCallTelemetry.IsEnabled) FacadeCallTelemetry.ObserveValue(\"path.point-count\", NativeDrawingBridge.GraphicsPathPoints(path.__handle).Length); __EnsureRecorder(); NativeDrawingBridge.DrawPath(__recorder, pen.__handle, path.__handle); }";
        if (method.Name == "FillPath") return "{ if (path is null) throw new global::System.ArgumentNullException(nameof(path)); if (FacadeCallTelemetry.IsEnabled) FacadeCallTelemetry.ObserveValue(\"path.point-count\", NativeDrawingBridge.GraphicsPathPoints(path.__handle).Length); __EnsureRecorder(); NativeDrawingBridge.FillPath(__recorder, brush.__handle, path.__handle); }";
        if (method.Name == "FillPolygon") return "{ if (points is null) throw new global::System.ArgumentNullException(nameof(points)); __EnsureRecorder(); var converted = new global::System.Drawing.PointF[points.Length]; for (var i = 0; i < points.Length; ++i) converted[i] = new global::System.Drawing.PointF(points[i].X, points[i].Y); NativeDrawingBridge.FillPolygon(__recorder, brush.__handle, converted); }";
        if (method.Name == "Save") return "{ __EnsureRecorder(); var state = new global::System.Drawing.Drawing2D.GraphicsState { __token = NativeDrawingBridge.RecorderSave(__recorder), __owner = this, __depth = ++__savedStateDepth, __hasClip = __hasClip, __clip = __clip, __smoothing = __smoothing, __interpolation = __interpolation, __pixelOffset = __pixelOffset, __compositing = __compositing, __compositingQuality = __compositingQuality }; state.__transform.__m11 = __transform.__m11; state.__transform.__m12 = __transform.__m12; state.__transform.__m21 = __transform.__m21; state.__transform.__m22 = __transform.__m22; state.__transform.__dx = __transform.__dx; state.__transform.__dy = __transform.__dy; return state; }";
        if (method.Name == "Restore") return "{ if (gstate is null) throw new global::System.ArgumentNullException(nameof(gstate)); if (!global::System.Object.ReferenceEquals(gstate.__owner, this) || gstate.__restored) throw new global::System.ArgumentException(\"GraphicsState does not belong to this Graphics or was already restored.\", nameof(gstate)); __EnsureRecorder(); NativeDrawingBridge.RecorderRestore(__recorder, gstate.__token); __savedStateDepth = global::System.Math.Max(0, gstate.__depth - 1); __hasClip = gstate.__hasClip; __clip = gstate.__clip; __transform = gstate.__transform; __smoothing = gstate.__smoothing; __interpolation = gstate.__interpolation; __pixelOffset = gstate.__pixelOffset; __compositing = gstate.__compositing; __compositingQuality = gstate.__compositingQuality; gstate.__restored = true; }";
        if (method.Name == "TranslateTransform") return "{ __transform.Translate(dx, dy); __EnsureRecorder(); NativeDrawingBridge.RecorderTranslate(__recorder, dx, dy); }";
        if (method.Name == "SetClip") return "{ __clip = new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height); __hasClip = true; __EnsureRecorder(); NativeDrawingBridge.RecorderSetClip(__recorder, __clip); }";
        if (method.Name == "Flush") return "{ NativeDrawingBridge.Flush(this); }";
        if (method.Name == "ReleaseHdc") return "{ NativeDrawingBridge.ReleaseHdc(this, hdc); }";
        if (method.Name == "GetHdc") return "{ return NativeDrawingBridge.GetHdc(this); }";
        if (method.Name == "IsVisible") return "{ __EnsureRecorder(); return NativeDrawingBridge.RecorderIsVisible(__recorder, point); }";
        if (method.Name == "DrawIcon") return "{ if (icon is null) throw new global::System.ArgumentNullException(nameof(icon)); using var image = icon.ToBitmap(); DrawImage(image, targetRect); }";
    }
    if (type.FullName == "System.Drawing.Drawing2D.GraphicsPath")
    {
        if (method.Name == "Dispose") return "{ if (__pathDisposed) return; __pathDisposed = true; global::System.Drawing.NativeDrawingBridge.Release(ref __handle); global::System.GC.SuppressFinalize(this); }";
        if (method.Name == "Reset") return "{ global::System.Drawing.NativeDrawingBridge.GraphicsPathReset(__handle); }";
        if (method.Name == "StartFigure") return "{ global::System.Drawing.NativeDrawingBridge.GraphicsPathStart(__handle); }";
        if (method.Name == "CloseFigure") return "{ global::System.Drawing.NativeDrawingBridge.GraphicsPathClose(__handle); }";
        if (method.Name == "AddLine")
        {
            if (method.GetParameters().Length == 2) return "{ global::System.Drawing.NativeDrawingBridge.GraphicsPathAddLine(__handle, pt1, pt2); }";
            return "{ global::System.Drawing.NativeDrawingBridge.GraphicsPathAddLine(__handle, new global::System.Drawing.PointF(x1, y1), new global::System.Drawing.PointF(x2, y2)); }";
        }
        if (method.Name == "AddRectangle") return "{ global::System.Drawing.NativeDrawingBridge.GraphicsPathAddRectangle(__handle, new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height)); }";
        if (method.Name == "AddEllipse") return "{ global::System.Drawing.NativeDrawingBridge.GraphicsPathAddEllipse(__handle, rect); }";
        if (method.Name == "AddArc")
        {
            if (method.GetParameters().Length == 3) return "{ global::System.Drawing.NativeDrawingBridge.GraphicsPathAddArc(__handle, new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height), startAngle, sweepAngle); }";
            return "{ global::System.Drawing.NativeDrawingBridge.GraphicsPathAddArc(__handle, new global::System.Drawing.RectangleF(x, y, width, height), startAngle, sweepAngle); }";
        }
        if (method.Name == "AddPath") return "{ if (addingPath is null) throw new global::System.ArgumentNullException(nameof(addingPath)); global::System.Drawing.NativeDrawingBridge.GraphicsPathAddPath(__handle, addingPath.__handle, connect); }";
        if (method.Name == "GetBounds") return "{ return global::System.Drawing.NativeDrawingBridge.GraphicsPathBounds(__handle); }";
        if (method.Name == "IsVisible") return "{ return global::System.Drawing.NativeDrawingBridge.GraphicsPathIsVisible(__handle, new global::System.Drawing.PointF(point.X, point.Y)); }";
        if (method.Name == "Clone") return "{ return global::System.Drawing.NativeDrawingBridge.GraphicsPathClone(__handle); }";
        if (method.Name == "Transform") return "{ if (matrix is null) throw new global::System.ArgumentNullException(nameof(matrix)); global::System.Drawing.NativeDrawingBridge.GraphicsPathTransform(__handle, matrix); }";
        if (method.Name == "AddString") return "{ if (s is null) throw new global::System.ArgumentNullException(nameof(s)); var width = global::System.Math.Max(0f, s.Length * emSize * 0.55f); global::System.Drawing.NativeDrawingBridge.GraphicsPathAddRectangle(__handle, new global::System.Drawing.RectangleF(origin.X, origin.Y - emSize, width, emSize * 1.2f)); }";
    }
    if (type.FullName == "System.Drawing.Drawing2D.Matrix")
    {
        if (method.Name == "Dispose") return "{ }";
        if (method.Name == "Translate") return "{ __dx += offsetX; __dy += offsetY; }";
        if (method.Name == "RotateAt") return "{ var radians = angle * (float)(global::System.Math.PI / 180.0); var cosine = (float)global::System.Math.Cos(radians); var sine = (float)global::System.Math.Sin(radians); __m11 = cosine; __m12 = sine; __m21 = -sine; __m22 = cosine; __dx = point.X - point.X * cosine + point.Y * sine; __dy = point.Y - point.X * sine - point.Y * cosine; }";
    }
    if (type.FullName == "System.Drawing.Region")
    {
        if (method.Name == "Dispose") return "{ if (__regionDisposed) return; __regionDisposed = true; NativeDrawingBridge.Release(ref __handle); global::System.GC.SuppressFinalize(this); }";
        if (method.Name == "GetBounds") return "{ if (__regionDisposed) throw new global::System.ObjectDisposedException(nameof(Region)); return NativeDrawingBridge.RegionBounds(__handle); }";
        if (method.Name == "Exclude") return "{ NativeDrawingBridge.RegionExclude(__handle, new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height)); }";
        if (method.Name == "Union" && method.GetParameters()[0].ParameterType.FullName == "System.Drawing.Rectangle") return "{ NativeDrawingBridge.RegionUnion(__handle, new global::System.Drawing.RectangleF(rect.X, rect.Y, rect.Width, rect.Height)); }";
        if (method.Name == "Union") return "{ if (path is null) throw new global::System.ArgumentNullException(nameof(path)); NativeDrawingBridge.RegionUnion(__handle, path.__handle); }";
    }
    if (type.FullName == "System.Drawing.Imaging.ImageAttributes")
    {
        if (method.Name == "Dispose") return "{ if (__attributesDisposed) return; __attributesDisposed = true; global::System.Drawing.NativeDrawingBridge.Release(ref __handle); global::System.GC.SuppressFinalize(this); }";
        if (method.Name == "SetColorMatrix") return "{ if (newColorMatrix is null) throw new global::System.ArgumentNullException(nameof(newColorMatrix)); global::System.Drawing.NativeDrawingBridge.ImageAttributesSetColorMatrix(__handle, newColorMatrix.__values); }";
        if (method.Name == "SetRemapTable") return "{ global::System.Drawing.NativeDrawingBridge.ImageAttributesSetRemap(__handle, map); }";
        if (method.Name == "Clone") return "{ return global::System.Drawing.NativeDrawingBridge.ImageAttributesClone(__handle); }";
    }
    if (type.FullName == "System.Windows.Forms.ButtonBase" && method.Name == "OnPaint")
        return "{ __PaintButtonSurface(pevent.Graphics); base.OnPaint(pevent); }";
    if (type.FullName == "System.Windows.Forms.BaseCollection" && method.Name == "GetEnumerator")
        return "{ return __Collection.GetEnumerator(); }";
    if (type.FullName == "System.Windows.Forms.BindingSource")
        return method.Name switch
        {
            "GetEnumerator" => "{ return __bindingList.GetEnumerator(); }",
            "Add" => "{ var result = __bindingList.Add(value); if (__position < 0) __position = 0; return result; }",
            "IndexOf" => "{ return __bindingList.IndexOf(value); }",
            "Clear" => "{ __bindingList.Clear(); __position = -1; }",
            "RemoveCurrent" => "{ if (__position < 0 || __position >= __bindingList.Count) return; __bindingList.RemoveAt(__position); if (__bindingList.Count == 0) __position = -1; else if (__position >= __bindingList.Count) __position = __bindingList.Count - 1; }",
            "ResetBindings" => "{ if (__bindingList.Count == 0) __position = -1; else if (__position < 0 || __position >= __bindingList.Count) __position = 0; }",
            _ => BodyFor(method.ReturnType, false),
        };
    if (type.FullName == "System.Windows.Forms.Application" && method.Name == "Run" &&
        method.GetParameters().Length == 1)
        return "{ if (mainForm is null) throw new global::System.ArgumentNullException(nameof(mainForm)); __RunContext(new ApplicationContext(mainForm)); }";
    if (type.FullName == "System.Windows.Forms.Application" && method.Name == "ExitThread")
        return "{ __context?.ExitThread(); }";
    if (type.FullName == "System.Windows.Forms.Application" && method.Name == "DoEvents")
        return "{ __CurrentForm?.__DoEvents(); }";
    if (type.FullName == "System.Windows.Forms.ScrollableControl")
    {
        if (method.Name == "OnMouseWheel")
            return "{ if (!__ScrollByWheel(e.Delta)) base.OnMouseWheel(e); }";
        if (method.Name == "OnLayout")
            return "{ base.OnLayout(levent); __RefreshScrollState(); }";
        if (method.Name == "OnScroll")
            return "{ Scroll?.Invoke(this, se); }";
        if (method.Name == "ScrollControlIntoView")
            return "{ if (activeControl is null || !Contains(activeControl) || !__autoScroll) return; __native.ScrollControlIntoView(activeControl.__native); __RefreshScrollState(); Invalidate(); }";
        if (method.Name == "SetAutoScrollMargin")
            return "{ __SetAutoScrollMargin(new global::System.Drawing.Size(x, y), true); }";
        if (method.Name == "SetDisplayRectLocation")
            return "{ __SetAutoScrollPosition(new global::System.Drawing.Point(global::System.Math.Max(0, -x), global::System.Math.Max(0, -y))); }";
        if (method.Name == "GetScrollState")
            return "{ __RefreshScrollState(); return (__scrollState & bit) != 0; }";
        if (method.Name == "SetScrollState")
            return "{ var known = ScrollStateAutoScrolling | ScrollStateHScrollVisible | ScrollStateVScrollVisible | ScrollStateUserHasScrolled | ScrollStateFullDrag; if (bit == 0 || (bit & ~known) != 0) throw new global::System.ArgumentOutOfRangeException(nameof(bit)); if ((bit & ScrollStateAutoScrolling) != 0) __SetAutoScroll(value); if ((bit & ScrollStateHScrollVisible) != 0) HScroll = value; if ((bit & ScrollStateVScrollVisible) != 0) VScroll = value; var passive = bit & (ScrollStateUserHasScrolled | ScrollStateFullDrag); if (value) __scrollState |= passive; else __scrollState &= ~passive; }";
        if (method.Name == "AdjustFormScrollbars")
            return "{ if (displayScrollbars) { __RefreshScrollState(); return; } if (!__autoScroll) { HScroll = false; VScroll = false; } }";
        if (method.Name == "ScrollToControl")
            return "{ if (activeControl is null) throw new global::System.ArgumentNullException(nameof(activeControl)); var current = __RefreshScrollState().Position; var bounds = activeControl.Bounds; var viewport = __RefreshScrollState().ViewportRectangle; var x = current.X; var y = current.Y; if (bounds.Left < current.X) x = bounds.Left; else if (bounds.Right > current.X + viewport.Width) x = bounds.Right - viewport.Width; if (bounds.Top < current.Y) y = bounds.Top; else if (bounds.Bottom > current.Y + viewport.Height) y = bounds.Bottom - viewport.Height; x = global::System.Math.Max(0, x - activeControl.AutoScrollOffset.X); y = global::System.Math.Max(0, y - activeControl.AutoScrollOffset.Y); return new global::System.Drawing.Point(x, y); }";
    }
    if (type.FullName == "System.Windows.Forms.Control")
    {
        if (method.Name == "Dispose" && method.GetParameters().Length == 1) return "{ if (disposing) { var children = new global::System.Collections.Generic.List<Control>(); foreach (Control child in Controls) children.Add(child); foreach (var child in children) child.Dispose(); if (global::System.Object.ReferenceEquals(__focusedControl, this)) { __focusedControl = null; OnLostFocus(global::System.EventArgs.Empty); Leave?.Invoke(this, global::System.EventArgs.Empty); } __parent?.Controls.Remove(this); __InvalidateManagedPaintSurface(); __ReleaseCompatibilityHandle(); __AbandonManagedPaintInput(this); __native.Dispose(); } base.Dispose(disposing); }";
        if (method.Name == "Show" && method.GetParameters().Length == 0) return "{ if (this is Form form) form.__ShowNonModal(); else Visible = true; }";
        if (method.Name == "Hide" && method.GetParameters().Length == 0) return "{ Visible = false; }";
        if (method.Name == "BeginInvoke") return "{ return __native.BeginInvoke(method); }";
        if (method.Name == "Invoke" && method.GetParameters().Length == 1 && method.ReturnType.FullName == "System.Void") return "{ __native.Invoke(method); }";
        if (method.Name == "Invoke" && method.GetParameters().Length == 1) return "{ return __native.Invoke(method); }";
        if (method.Name == "BringToFront") return "{ __parent?.Controls.__BringToFront(this); }";
        if (method.Name == "SendToBack") return "{ __parent?.Controls.__SendToBack(this); }";
        if (method.Name == "Contains") return "{ return ctl is not null && __ContainsDescendant(ctl); }";
        if (method.Name == "GetChildAtPoint")
        {
            if (method.GetParameters().Length == 1)
                return "{ return GetChildAtPoint(pt, GetChildAtPointSkip.None); }";
            return "{ var unknown = (int)skipValue & ~7; if (unknown != 0) throw new global::System.ComponentModel.InvalidEnumArgumentException(nameof(skipValue), (int)skipValue, typeof(GetChildAtPointSkip)); for (var index = 0; index < Controls.Count; ++index) { var child = Controls[index]; if ((skipValue & GetChildAtPointSkip.Invisible) != 0 && !child.Visible) continue; if ((skipValue & GetChildAtPointSkip.Disabled) != 0 && !child.Enabled) continue; if ((skipValue & GetChildAtPointSkip.Transparent) != 0 && child.BackColor.A == 0 && child.GetStyle(ControlStyles.SupportsTransparentBackColor)) continue; if (child.Bounds.Contains(pt)) return child; } return null!; }";
        }
        if (method.Name == "GetNextControl") return "{ var ordered = new global::System.Collections.Generic.List<Control>(); __CollectTabOrder(ordered); if (ordered.Count == 0) return null!; if (ctl is null || global::System.Object.ReferenceEquals(ctl, this)) return forward ? ordered[0] : ordered[^1]; var index = ordered.IndexOf(ctl); if (index < 0) return null!; index += forward ? 1 : -1; return index >= 0 && index < ordered.Count ? ordered[index] : null!; }";
        if (method.Name == "GetPreferredSize") return "{ var preferred = __PreferredLayoutSize(); var width = proposedSize.Width > 0 ? global::System.Math.Min(preferred.Width, proposedSize.Width) : preferred.Width; var height = proposedSize.Height > 0 ? global::System.Math.Min(preferred.Height, proposedSize.Height) : preferred.Height; return new global::System.Drawing.Size(global::System.Math.Max(__minimumSize.Width, width), global::System.Math.Max(__minimumSize.Height, height)); }";
        if (method.Name == "GetAutoSizeMode") return "{ return __autoSizeMode; }";
        if (method.Name == "SetAutoSizeMode") return "{ if (!global::System.Enum.IsDefined(mode)) throw new global::System.ComponentModel.InvalidEnumArgumentException(nameof(mode), (int)mode, typeof(AutoSizeMode)); if (__autoSizeMode == mode) return; __autoSizeMode = mode; if (__autoSize) __RefreshAutoSizeFromChildren(); }";
        if (method.Name == "SetBounds")
        {
            if (method.GetParameters().Length == 4)
                return "{ SetBounds(x, y, width, height, BoundsSpecified.All); }";
            return "{ var current = Bounds; var unknown = (int)specified & ~15; if (unknown != 0) throw new global::System.ComponentModel.InvalidEnumArgumentException(nameof(specified), (int)specified, typeof(BoundsSpecified)); if ((specified & BoundsSpecified.X) != 0) current.X = x; if ((specified & BoundsSpecified.Y) != 0) current.Y = y; if ((specified & BoundsSpecified.Width) != 0) current.Width = width; if ((specified & BoundsSpecified.Height) != 0) current.Height = height; SetBoundsCore(current.X, current.Y, current.Width, current.Height, specified); }";
        }
        if (method.Name == "SetBoundsCore") return "{ var current = Bounds; if ((specified & BoundsSpecified.X) != 0) current.X = x; if ((specified & BoundsSpecified.Y) != 0) current.Y = y; if ((specified & BoundsSpecified.Width) != 0) current.Width = width; if ((specified & BoundsSpecified.Height) != 0) current.Height = height; Bounds = current; }";
        if (method.Name == "Focus") return "{ if (!__CanFocus || !Enabled || !Visible || __native.IsDisposed) return false; for (var ancestor = Parent; ancestor is not null; ancestor = ancestor.Parent) if (!ancestor.Enabled || !ancestor.Visible) return false; var previous = __focusedControl; if (global::System.Object.ReferenceEquals(previous, this)) return true; __focusedControl = this; if (previous is not null) { previous.OnLostFocus(global::System.EventArgs.Empty); previous.Leave?.Invoke(previous, global::System.EventArgs.Empty); } Enter?.Invoke(this, global::System.EventArgs.Empty); OnGotFocus(global::System.EventArgs.Empty); return true; }";
        if (method.Name == "FindForm") return "{ for (Control? current = this; current is not null; current = current.Parent) if (current is Form form) return form; return null!; }";
        if (method.Name == "SuspendLayout") return "{ ++__layoutSuspendDepth; __native.SuspendLayout(); }";
        if (method.Name == "ResumeLayout" && method.GetParameters().Length == 0) return "{ ResumeLayout(true); }";
        if (method.Name == "ResumeLayout" && method.GetParameters().Length == 1) return "{ if (__layoutSuspendDepth == 0) return; --__layoutSuspendDepth; __native.ResumeLayout(false); if (__layoutSuspendDepth != 0) return; if (performLayout) __DrainLayout(); else { __layoutDeferred = false; __pendingLayoutArgs = null; } }";
        if (method.Name == "PerformLayout" && method.GetParameters().Length == 0) return "{ __QueueLayout(new LayoutEventArgs(this, null)); }";
        if (method.Name == "PerformLayout") return "{ __QueueLayout(new LayoutEventArgs(affectedControl, affectedProperty)); }";
        if (method.Name == "Invalidate")
        {
            var parameters = method.GetParameters();
            if (parameters.Length == 0) return "{ __InvalidateCore(null, false); }";
            if (parameters.Length == 1 && parameters[0].ParameterType.FullName == "System.Boolean") return "{ __InvalidateCore(null, invalidateChildren); }";
            if (parameters[0].ParameterType.FullName == "System.Drawing.Rectangle") return parameters.Length == 1
                ? "{ __InvalidateCore(rc, false); }"
                : "{ __InvalidateCore(rc, invalidateChildren); }";
            return parameters.Length == 1
                ? "{ __InvalidateCore(region is null ? null : __RegionDamage(region), false); }"
                : "{ __InvalidateCore(region is null ? null : __RegionDamage(region), invalidateChildren); }";
        }
        if (method.Name == "Update") return "{ if (__native.IsDisposed) return; var form = FindForm(); if (form is not null && !form.Visible) return; global::System.Action drain = () => { __RenderManagedPaintNow(); __DrainWindowSurfaceNow(); }; if (InvokeRequired) Invoke(drain); else drain(); }";
        if (method.Name == "UpdateStyles") return "{ }";
        if (method.Name == "Refresh") return "{ Invalidate(); Update(); }";
        if (method.Name == "GetStyle") return "{ return __GetStyle(flag); }";
        if (method.Name == "SetStyle") return "{ __SetStyle(flag, value); }";
        if (method.Name == "OnPaintBackground") return "{ pevent.Graphics.Clear(__ManagedPaintBackground()); __PaintManagedBackgroundImage(pevent.Graphics); }";
        if (method.Name == "InvokePaintBackground") return "{ c.OnPaintBackground(e); }";
        if (method.Name == "NotifyInvalidate") return "{ OnInvalidated(new InvalidateEventArgs(invalidatedArea)); }";
        if (method.Name == "OnInvalidated") return "{ Invalidated?.Invoke(this, e); }";
        if (method.Name == "ResetBackColor") return "{ if (__backColor.IsEmpty) return; __backColor = global::System.Drawing.Color.Empty; __ApplyEffectiveColors(); OnBackColorChanged(global::System.EventArgs.Empty); if (__loadRaised) __QueueManagedPaint(); }";
        if (method.Name == "PointToScreen") return "{ var offset = __ScreenOffset(); return new global::System.Drawing.Point(p.X + offset.X, p.Y + offset.Y); }";
        if (method.Name == "PointToClient") return "{ var offset = __ScreenOffset(); return new global::System.Drawing.Point(p.X - offset.X, p.Y - offset.Y); }";
        if (method.Name == "RectangleToScreen") return "{ var offset = __ScreenOffset(); return new global::System.Drawing.Rectangle(r.X + offset.X, r.Y + offset.Y, r.Width, r.Height); }";
        if (method.Name == "RectangleToClient") return "{ var offset = __ScreenOffset(); return new global::System.Drawing.Rectangle(r.X - offset.X, r.Y - offset.Y, r.Width, r.Height); }";
        if (method.Name == "OnKeyDown") return "{ KeyDown?.Invoke(this, e); }";
        if (method.Name == "OnEnabledChanged") return "{ EnabledChanged?.Invoke(this, e); }";
        if (method.Name == "OnFontChanged") return "{ FontChanged?.Invoke(this, e); }";
        if (method.Name == "OnForeColorChanged") return "{ ForeColorChanged?.Invoke(this, e); }";
        if (method.Name == "OnGotFocus") return "{ GotFocus?.Invoke(this, e); }";
        if (method.Name == "OnHandleDestroyed") return "{ HandleDestroyed?.Invoke(this, e); }";
        if (method.Name == "OnLostFocus") return "{ LostFocus?.Invoke(this, e); }";
        if (method.Name == "OnMouseDown") return "{ MouseDown?.Invoke(this, e); }";
        if (method.Name == "OnMouseEnter") return "{ MouseEnter?.Invoke(this, e); }";
        if (method.Name == "OnMouseHover") return "{ MouseHover?.Invoke(this, e); }";
        if (method.Name == "OnMouseLeave") return "{ MouseLeave?.Invoke(this, e); }";
        if (method.Name == "OnMouseMove") return "{ MouseMove?.Invoke(this, e); }";
        if (method.Name == "OnMouseUp") return "{ MouseUp?.Invoke(this, e); }";
        if (method.Name == "OnMouseWheel") return "{ MouseWheel?.Invoke(this, e); }";
        if (method.Name == "OnPaint") return "{ Paint?.Invoke(this, e); }";
        if (method.Name == "OnParentChanged") return "{ ParentChanged?.Invoke(this, e); }";
        if (method.Name == "OnResize") return "{ Resize?.Invoke(this, e); }";
        if (method.Name == "OnSizeChanged") return "{ SizeChanged?.Invoke(this, e); }";
        if (method.Name == "OnLayout") return "{ if (__LayoutAutoSize && !__assigningLayoutBounds) __RefreshAutoSizeFromChildren(); __ApplyDockLayout(); Layout?.Invoke(this, levent); }";
        if (method.Name == "ProcessDialogKey") return "{ return FindForm()?.__ProcessDialogKey(keyData) ?? false; }";
    }
    if (type.FullName == "System.Windows.Forms.ComboBox" && method.Name == "Select")
        return "{ __SetComboSelection(start, length); }";
    if (type.FullName == "System.Windows.Forms.TextBoxBase")
        return method.Name switch
        {
            "AppendText" => "{ if (text is null) return; __SetTextSelection(Text.Length, 0); __ReplaceTextSelection(text); }",
            "Clear" => "{ if (__textReadOnly) return; __SetTextSelection(0, Text.Length); __ReplaceTextSelection(string.Empty); }",
            "DeselectAll" => "{ __NormalizeTextSelection(); __textAnchor = __textCaret; __SetNativeFieldEditState(__textAnchor, __textCaret); Invalidate(); }",
            "Select" => "{ __SetTextSelection(start, length); }",
            "SelectAll" => "{ __SetTextSelection(0, Text.Length); }",
            _ => BodyFor(method.ReturnType, false),
        };
    if (type.FullName == "System.Windows.Forms.Form" && method.Name == "Dispose" &&
        method.GetParameters().Length == 1)
        return "{ if (disposing) { __RestoreHostedRootProjection(); foreach (var child in __ownedForms.ToArray()) child.Dispose(); __ownedForms.Clear(); __ownerForm?.__ownedForms.Remove(this); __ownerForm = null; __acceptButton?.NotifyDefault(false); __acceptButton = null; __cancelButton = null; __RestoreHostedCaptionInset(); if (__hostedCaptionPanel is not null) { __hostedCaptionPanel.Parent?.Controls.Remove(__hostedCaptionPanel); __hostedCaptionPanel.Dispose(); } __hostedCaptionPanel = null; __hostedCaptionLabel = null; __hostedCaptionClose = null; __hostedProjectionRoot = null; } base.Dispose(disposing); }";
    if (method.Name == "Dispose" && method.GetParameters().Length == 1 &&
        method.GetParameters()[0].ParameterType.FullName == "System.Boolean")
        return "{ base.Dispose(disposing); }";
    if (type.FullName == "System.Windows.Forms.Form")
    {
        if (method.Name == "Close") return "{ __CloseNonModalOrRequest(); }";
        if (method.Name == "OnLayout") return "{ base.OnLayout(levent); __ApplyHostedCaptionLayout(); __RaiseOwnedHostedProjections(); }";
        if (method.Name == "OnPaint") return "{ base.OnPaint(e); }";
        if (method.Name == "ShowDialog") return "{ if (__TraceLifecycle) global::System.Console.Error.WriteLine(\"facade-dialog=show|type=\" + GetType().FullName + \"|name=\" + Name + \"|text=\" + Text + \"|size=\" + Width + \"x\" + Height); return __ShowDialogCore(null); }";
        if (method.Name == "SetBoundsCore") return "{ Bounds = new global::System.Drawing.Rectangle(x, y, width, height); }";
        if (method.Name == "OnFormClosing") return "{ FormClosing?.Invoke(this, e); }";
        if (method.Name == "OnActivated") return "{ }";
        if (method.Name == "Activate") return "{ Focus(); }";
    }
    if (type.FullName == "System.Windows.Forms.Control+ControlCollection")
    {
        // WinForms ControlCollection.Add appends and preserves insertion order.
        // Z-order mutations are explicit through SetChildIndex/BringToFront;
        // reversing Add here corrupts designer-authored Dock and table order.
        return method.Name switch
        {
            "Add" => "{ if (value is null) throw new global::System.ArgumentNullException(nameof(value)); if (global::System.Object.ReferenceEquals(value, __owner)) throw new global::System.ArgumentException(\"A control cannot parent itself.\", nameof(value)); if (global::System.Object.ReferenceEquals(value.__parent, __owner)) return; value.__parent?.Controls.Remove(value); __owner.__native.AddChild(value.__native); __items.Add(value); __owner.__native.SetChildIndex(value.__native, __items.Count - 1); value.__parent = __owner; value.__ApplyEffectiveColors(true); value.OnParentChanged(global::System.EventArgs.Empty); if (__owner is Panel && __owner is not TableLayoutPanel && __owner.Height == 0 && value.Visible && value.Dock is DockStyle.Top or DockStyle.Bottom) { var preferred = value.__PreferredLayoutSize(); if (preferred.Height > 0) __owner.Height = preferred.Height + __owner.Padding.Top + __owner.Padding.Bottom; } __owner.PerformLayout(); if (__owner.__HasRaisedLoad) value.__RaiseLoad(); }",
            "AddRange" => "{ foreach (var child in controls) Add(child); }",
            "Remove" => "{ if (__items.Remove(value)) { __owner.__native.RemoveChild(value.__native); value.__parent = null; value.OnParentChanged(global::System.EventArgs.Empty); } }",
            "Clear" => "{ foreach (var child in __items.ToArray()) Remove(child); }",
            "Contains" => "{ return __items.Contains(control); }",
            "IndexOf" => "{ return __items.IndexOf(control); }",
            "GetChildIndex" => method.GetParameters().Length == 1
                ? "{ return GetChildIndex(child, true); }"
                : "{ var index = __items.IndexOf(child); if (index < 0 && throwException) throw new global::System.ArgumentException(\"Control is not a child.\", nameof(child)); return index; }",
            "SetChildIndex" => "{ if (newIndex < 0) throw new global::System.ArgumentOutOfRangeException(nameof(newIndex)); if (!__items.Remove(child)) throw new global::System.ArgumentException(\"Control is not a child.\", nameof(child)); var bounded = global::System.Math.Min(newIndex, __items.Count); __items.Insert(bounded, child); __owner.__native.SetChildIndex(child.__native, bounded); __owner.PerformLayout(); }",
            _ => BodyFor(method.ReturnType, false),
        };
    }
    if (type.FullName == "System.Windows.Forms.TableLayoutPanel")
        return method.Name switch
        {
            "SetColumnSpan" => "{ if (value < 1) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __columnSpans[control] = value; PerformLayout(); }",
            "SetRowSpan" => "{ if (value < 1) throw new global::System.ArgumentOutOfRangeException(nameof(value)); __rowSpans[control] = value; PerformLayout(); }",
            _ => BodyFor(method.ReturnType, false),
        };
    if (type.FullName == "System.Windows.Forms.TableLayoutControlCollection" && method.Name == "Add")
        return "{ __tableOwner?.__SetCell(control, column, row); base.Add(control); }";
    if (type.FullName == "System.Windows.Forms.DataGridView")
        return method.Name switch
        {
            "DisplayedRowCount" => "{ return Rows.Count; }",
            "ClearSelection" => "{ SelectionChanged?.Invoke(this, global::System.EventArgs.Empty); }",
            "NotifyCurrentCellDirty" => "{ }",
            _ => BodyFor(method.ReturnType, false),
        };
    if (type.FullName == "System.Windows.Forms.DataGridViewColumnCollection" && method.Name == "AddRange")
        return "{ if (dataGridViewColumns is null) throw new global::System.ArgumentNullException(nameof(dataGridViewColumns)); foreach (var column in dataGridViewColumns) { if (column is null) throw new global::System.ArgumentNullException(nameof(dataGridViewColumns)); column.__dataGridView = __owner; __items.Add(column); } }";
    if (type.FullName == "System.Windows.Forms.TableLayoutColumnStyleCollection" && method.Name == "Add")
        return "{ if (columnStyle is null) throw new global::System.ArgumentNullException(nameof(columnStyle)); __styles.Add(columnStyle); return __styles.Count - 1; }";
    if (type.FullName == "System.Windows.Forms.TableLayoutRowStyleCollection" && method.Name == "Add")
        return "{ if (rowStyle is null) throw new global::System.ArgumentNullException(nameof(rowStyle)); __styles.Add(rowStyle); return __styles.Count - 1; }";
    if (type.FullName == "System.Windows.Forms.ComboBox+ObjectCollection")
        return method.Name switch
        {
            "Add" => "{ if (item is null) throw new global::System.ArgumentNullException(nameof(item)); __items.Add(item); __owner?.Invalidate(); return __items.Count - 1; }",
            "AddRange" => "{ if (items is null) throw new global::System.ArgumentNullException(nameof(items)); foreach (var item in items) Add(item); }",
            "Clear" => "{ __items.Clear(); __owner?.__ResetItemsSelection(); }",
            "GetEnumerator" => "{ return __items.GetEnumerator(); }",
            "IndexOf" => "{ return __items.IndexOf(value); }",
            "Insert" => "{ if (item is null) throw new global::System.ArgumentNullException(nameof(item)); __items.Insert(index, item); if (__owner is not null && __owner.SelectedIndex >= index) __owner.__SetSelectedIndexSilently(__owner.SelectedIndex + 1); __owner?.Invalidate(); }",
            "RemoveAt" => "{ var selected = __owner?.SelectedIndex ?? -1; __items.RemoveAt(index); if (__owner is not null) { if (selected == index) __owner.SelectedIndex = -1; else if (selected > index) __owner.__SetSelectedIndexSilently(selected - 1); __owner.Invalidate(); } }",
            _ => BodyFor(method.ReturnType, false),
        };
    if (type.FullName == "System.Windows.Forms.ListBox+ObjectCollection")
        return method.Name switch
        {
            "Add" => "{ if (item is null) throw new global::System.ArgumentNullException(nameof(item)); __items.Add(item); __owner?.Invalidate(); return __items.Count - 1; }",
            "Insert" => "{ if (item is null) throw new global::System.ArgumentNullException(nameof(item)); __items.Insert(index, item); if (__owner is not null && __owner.SelectedIndex >= index) __owner.SelectedIndex += 1; __owner?.Invalidate(); }",
            "RemoveAt" => "{ __items.RemoveAt(index); if (__owner is not null && __owner.SelectedIndex >= __items.Count) __owner.SelectedIndex = __items.Count - 1; __owner?.Invalidate(); }",
            _ => BodyFor(method.ReturnType, false),
        };
    if (type.FullName == "System.Windows.Forms.Timer")
        return method.Name switch
        {
            "Start" => "{ Enabled = true; }",
            "Stop" => "{ Enabled = false; }",
            _ => BodyFor(method.ReturnType, false),
        };
    if (type.FullName == "System.Windows.Forms.ToolStripItemCollection")
        return method.Name switch
        {
            "Add" when method.GetParameters().Length == 1 => "{ if (value is null) throw new global::System.ArgumentNullException(nameof(value)); value.__owner = __owner; __items.Add(value); __owner?.__RefreshItems(); return __items.Count - 1; }",
            "Add" when method.GetParameters().Length == 2 => "{ var item = new ToolStripMenuItem(text) { Image = image }; Add(item); return item; }",
            "AddRange" => "{ foreach (var item in toolStripItems) Add(item); }",
            "Clear" => "{ foreach (var item in __items) item.__owner = null; __items.Clear(); __owner?.__RefreshItems(); }",
            _ => BodyFor(method.ReturnType, false),
        };
    if (method.ReturnType.FullName == "System.Void" && method.GetParameters().Length == 1 &&
        OverridesBase(method) && method.Name is "OnLayout" or "OnResize" or "OnSizeChanged" or
            "OnParentChanged" or "OnPaint" or "OnTextChanged" or "OnVisibleChanged")
        return $"{{ base.{method.Name}({Escape(method.GetParameters()[0].Name ?? "e")}); }}";
    return BodyFor(method.ReturnType, type.IsValueType);
}

static string BodyFor(Type? returnType, bool structConstructor) => structConstructor ? "{ this = default; }" :
    returnType is null || returnType.FullName == "System.Void" ? "{ }" : "{ return default!; }";

static bool OverridesBase(MethodInfo method) => method.IsVirtual &&
    (method.Attributes & MethodAttributes.NewSlot) == 0;

static bool CanEmitOverride(Type type, MethodInfo method,
                            Dictionary<Type, HashSet<MemberInfo>> selected)
{
    if (!OverridesBase(method)) return false;
    const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic |
        BindingFlags.Instance | BindingFlags.DeclaredOnly;
    for (var current = type.BaseType; current is not null; current = current.BaseType)
    {
        var candidate = current.GetMethods(flags).FirstOrDefault(value =>
            value.Name == method.Name && MemberSignature(value) == MemberSignature(method));
        if (candidate is null) continue;
        var assembly = current.Assembly.GetName().Name;
        if (current.FullName == "System.Windows.Forms.Control" &&
            method.Name is "OnTextChanged" or "OnVisibleChanged")
            return true;
        if (assembly is not ("System.Windows.Forms" or "System.Windows.Forms.Primitives"))
            return true;
        if (selected.TryGetValue(current, out var members) && members.Contains(candidate))
            return true;
        // A compatibility catalogue can omit an intermediate framework
        // override (for example ContainerControl.OnResize) while retaining the
        // grand-base virtual slot. Keep walking so the emitted descendant joins
        // that existing slot instead of accidentally creating a new one.
    }
    return false;
}

static string Access(MethodBase method) => method.IsPublic ? "public" : method.IsFamily || method.IsFamilyOrAssembly ? "protected" : "public";

static string Parameters(ParameterInfo[] parameters, bool includeDefault) => string.Join(", ", parameters.Select((parameter, index) =>
{
    var type = parameter.ParameterType;
    var prefix = "";
    if (type.IsByRef) { prefix = parameter.IsOut ? "out " : parameter.IsIn ? "in " : "ref "; type = type.GetElementType()!; }
    var value = prefix + CsType(type) + " " + Escape(string.IsNullOrEmpty(parameter.Name) ? "p" + index : parameter.Name!);
    if (includeDefault && parameter.HasDefaultValue) value += " = default";
    return value;
}));

static string CsType(Type type)
{
    if (type.IsByRef) return CsType(type.GetElementType()!);
    if (type.IsPointer) return "nint";
    if (type.IsArray) return CsType(type.GetElementType()!) + "[" + new string(',', type.GetArrayRank() - 1) + "]";
    if (type.IsGenericParameter) return type.Name;
    if (type.IsGenericType)
    {
        var name = type.GetGenericTypeDefinition().FullName!.Split('`')[0].Replace('+', '.');
        return "global::" + name + "<" + string.Join(", ", type.GetGenericArguments().Select(CsType)) + ">";
    }
    return type.FullName switch
    {
        "System.Void" => "void", "System.Boolean" => "bool", "System.Byte" => "byte", "System.SByte" => "sbyte",
        "System.Int16" => "short", "System.UInt16" => "ushort", "System.Int32" => "int", "System.UInt32" => "uint",
        "System.Int64" => "long", "System.UInt64" => "ulong", "System.Single" => "float", "System.Double" => "double",
        "System.Decimal" => "decimal", "System.Char" => "char", "System.String" => "string", "System.Object" => "object",
        "System.IntPtr" => "nint", "System.UIntPtr" => "nuint", _ => "global::" + (type.FullName ?? type.Name).Replace('+', '.')
    };
}

static string CleanName(Type type)
{
    var name = type.Name;
    var tick = name.IndexOf('`');
    if (tick < 0) return Escape(name);
    var args = type.GetGenericArguments().Where(arg => arg.DeclaringType == type).Select(arg => arg.Name);
    return Escape(name[..tick]) + "<" + string.Join(",", args) + ">";
}

static string Escape(string name) => Regex.IsMatch(name, "^(event|object|string|ref|out|in|base|this|params|internal|public|private|protected|new|default|operator|checked|unchecked|fixed|lock|is|as)$") ? "@" + name : name;

static string ProjectFile(string assemblyName)
{
    var projectReferences = assemblyName switch
    {
        "System.Windows.Forms" =>
            "    <ProjectReference Include=\"../System.Windows.Forms.Primitives/System.Windows.Forms.Primitives.csproj\" />\n" +
            "    <ProjectReference Include=\"../System.Drawing.Common/System.Drawing.Common.csproj\" />\n",
        "System.Windows.Forms.Primitives" =>
            "    <ProjectReference Include=\"../System.Drawing.Common/System.Drawing.Common.csproj\" />\n",
        _ => "",
    };
    var windowsReferences = assemblyName == "System.Drawing.Common" ? "" : $"""
    <Reference Include="System.Private.Windows.Core"><HintPath>$(GuiFormsWindowsDesktopRef)/System.Private.Windows.Core.dll</HintPath></Reference>
    <Reference Include="System.Private.Windows.GdiPlus"><HintPath>$(GuiFormsWindowsDesktopRef)/System.Private.Windows.GdiPlus.dll</HintPath></Reference>
    <Reference Include="Accessibility"><HintPath>$(GuiFormsWindowsDesktopRef)/Accessibility.dll</HintPath></Reference>
""";
    var windowsProperty = assemblyName == "System.Drawing.Common" ? "" :
        "\n    <GuiFormsWindowsDesktopRef Condition=\"'$(GuiFormsWindowsDesktopRef)' == ''\">$(GUI_FORMS_WINDOWS_DESKTOP_REF)</GuiFormsWindowsDesktopRef>";
    var windowsValidation = assemblyName == "System.Drawing.Common" ? "" :
        "\n  <Target Name=\"ValidateGuiFormsWindowsDesktopRef\" BeforeTargets=\"ResolveAssemblyReferences\"\n" +
        "          Condition=\"'$(GuiFormsWindowsDesktopRef)' == ''\">\n" +
        "    <Error Text=\"Set GuiFormsWindowsDesktopRef or GUI_FORMS_WINDOWS_DESKTOP_REF to the .NET 10 WindowsDesktop reference directory.\" />\n" +
        "  </Target>";
    return $"""
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>{assemblyName}</AssemblyName>
    <RootNamespace>{(assemblyName == "System.Drawing.Common" ? "System.Drawing" : "System.Windows.Forms")}</RootNamespace>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <DebugType>none</DebugType>
    <DebugSymbols>false</DebugSymbols>
    <Deterministic>true</Deterministic>
    <GenerateAssemblyInfo>false</GenerateAssemblyInfo>{windowsProperty}
  </PropertyGroup>
  <ItemGroup>
{projectReferences}{windowsReferences}
  </ItemGroup>{windowsValidation}
</Project>
""";
}

static string DrawingBridgeSource() => """
// <auto-generated/>
#nullable enable
using System.Runtime.InteropServices;
using System.Text;

namespace System.Drawing;

internal static unsafe class NativeDrawingBridge
{
    private static readonly bool traceImageContent =
        global::System.Environment.GetEnvironmentVariable("GUI_DRAWING_TRACE_IMAGE_CONTENT") == "1";
    private static readonly global::System.Collections.Concurrent.ConcurrentDictionary<string, int>
        tracedImageDimensions = new(global::System.StringComparer.Ordinal);
    private static int nativeSurfaceCaptureTraceCount;
    private static int nativeSurfacePresentTraceCount;
    [StructLayout(LayoutKind.Sequential)] internal record struct Handle(uint Slot, uint Generation)
    {
        internal readonly bool IsNull => Slot == 0;
    }
    [StructLayout(LayoutKind.Sequential)] private struct NativeColor { internal uint Argb, IsEmpty; }
    [StructLayout(LayoutKind.Sequential)] private struct Point { internal double X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct Rect { internal double X, Y, Width, Height; }
    [StructLayout(LayoutKind.Sequential)] private struct RectI { internal int X, Y, Width, Height; }
    [StructLayout(LayoutKind.Sequential)] private struct NativeSize { internal double Width, Height; }
    [StructLayout(LayoutKind.Sequential)] private struct MatrixValue { internal double M11, M12, M21, M22, Dx, Dy; }
    [StructLayout(LayoutKind.Sequential)] private struct BitmapLockView
    {
        internal void* Data;
        internal void* WritableData;
        internal ulong RowBytes;
        internal uint Width, Height, PixelFormat;
        internal ulong Token;
    }
    [StructLayout(LayoutKind.Sequential)] private struct ColorRemap
    {
        internal NativeColor OldColor, NewColor;
    }
    [StructLayout(LayoutKind.Sequential)] private struct StringView { internal byte* Data; internal ulong Size; }
    [StructLayout(LayoutKind.Sequential)] private struct Api
    {
        internal uint StructSize, AbiVersion;
        internal fixed ulong Entries[104];
    }

    [DllImport("gui_drawing_abi0", EntryPoint = "gd_get_api_v0", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetApi(uint requestedVersion, ref Api api);
    [DllImport("gui_drawing_raster0", EntryPoint = "gdr_initialize", CallingConvention = CallingConvention.Cdecl)]
    private static extern int InitializeRaster();
    [DllImport("user32.dll")]
    private static extern nint WindowFromDC(nint device);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint SendMessageW(nint window, uint message, nint wParam, nint lParam);
    private const uint NativeSurfacePresentedMessage = 0x83f1u;

    private static Api api = Load();

    private static Api Load()
    {
        var value = new Api { StructSize = (uint)sizeof(Api) };
        Check(GetApi(2, ref value));
        if (value.AbiVersion != 2 || value.StructSize < sizeof(Api))
            throw new InvalidOperationException("GUI.Drawing ABI 0.2 table is incomplete.");
        return value;
    }

    private static int rasterInitialized;
    private static readonly object rasterLock = new();
    private static void EnsureRaster()
    {
        if (global::System.Threading.Volatile.Read(ref rasterInitialized) != 0) return;
        lock (rasterLock)
        {
            if (rasterInitialized != 0) return;
            Check(InitializeRaster());
            global::System.Threading.Volatile.Write(ref rasterInitialized, 1);
        }
    }

    private static nint Entry(int index)
    {
        fixed (ulong* entries = api.Entries) return (nint)entries[index];
    }

    private static void Check(int result)
    {
        if (result != 0) throw new InvalidOperationException("GUI.Drawing native operation failed with result " + result + ".");
    }

    private static NativeColor Native(global::System.Drawing.Color color) =>
        new() { Argb = unchecked((uint)color.ToArgb()), IsEmpty = color.IsEmpty ? 1u : 0u };
    private static Rect Native(global::System.Drawing.RectangleF value) =>
        new() { X = value.X, Y = value.Y, Width = value.Width, Height = value.Height };

    internal static void Release(ref Handle handle)
    {
        if (handle.IsNull) return;
        Check(((delegate* unmanaged[Cdecl]<Handle, int>)Entry(2))(handle));
        handle = default;
    }

    internal static Handle BitmapCreate(int width, int height)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<uint, uint, uint, Handle*, int>)Entry(49))(
            checked((uint)width), checked((uint)height), 0, &handle));
        FacadeCallTelemetry.Observe("bitmap.create-dimensions", width.ToString(global::System.Globalization.CultureInfo.InvariantCulture) + "x" + height.ToString(global::System.Globalization.CultureInfo.InvariantCulture));
        FacadeCallTelemetry.Observe("bitmap.storage-format", "premultiplied-bgra32");
        return handle;
    }

    internal static Handle BitmapClone(Handle source, int x, int y, int width, int height)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<Handle, RectI, Handle*, int>)Entry(55))(
            source, new RectI { X = x, Y = y, Width = width, Height = height }, &handle));
        return handle;
    }

    private static void Dimensions(Handle bitmap, out int width, out int height)
    {
        uint w, h, format;
        ulong generation;
        Check(((delegate* unmanaged[Cdecl]<Handle, uint*, uint*, uint*, ulong*, int>)Entry(50))(
            bitmap, &w, &h, &format, &generation));
        width = checked((int)w);
        height = checked((int)h);
    }

    internal static global::System.Drawing.Color BitmapGetPixel(Handle bitmap, int x, int y)
    {
        NativeColor color;
        Check(((delegate* unmanaged[Cdecl]<Handle, uint, uint, NativeColor*, int>)Entry(51))(
            bitmap, checked((uint)x), checked((uint)y), &color));
        return global::System.Drawing.Color.FromArgb(unchecked((int)color.Argb));
    }

    internal static void BitmapMakeTransparent(Handle bitmap, global::System.Drawing.Color color) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, NativeColor, int>)Entry(56))(bitmap, Native(color)));

    internal static global::System.Drawing.Bitmap CloneBitmap(global::System.Drawing.Image image)
    {
        var result = new global::System.Drawing.Bitmap();
        result.__width = image.Width;
        result.__height = image.Height;
        result.__pixelFormat = image.PixelFormat;
        result.__bitmap = BitmapClone(image.__BitmapHandle, 0, 0, image.Width, image.Height);
        return result;
    }

    internal static global::System.Drawing.Bitmap Thumbnail(global::System.Drawing.Image image, int width, int height)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<Handle, uint, uint, Handle*, int>)Entry(57))(
            image.__BitmapHandle, checked((uint)width), checked((uint)height), &handle));
        return WrapBitmap(handle, width, height);
    }

    internal static Handle SolidBrushCreate(global::System.Drawing.Color color)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<NativeColor, Handle*, int>)Entry(6))(Native(color), &handle));
        return handle;
    }

    internal static Handle PenCreate(global::System.Drawing.Color color, float width)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<NativeColor, double, Handle*, int>)Entry(7))(
            Native(color), width, &handle));
        return handle;
    }
    internal static void PenSetWidth(Handle pen, float width) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, double, int>)Entry(8))(pen, width));
    internal static void PenSetDashStyle(Handle pen, uint style) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)Entry(9))(pen, style));
    internal static void PenSetDashPattern(Handle pen, float[] pattern)
    {
        if (pattern is null) throw new ArgumentNullException(nameof(pattern));
        var values = new double[pattern.Length];
        for (var index = 0; index < pattern.Length; ++index) values[index] = pattern[index];
        fixed (double* pointer = values)
            Check(((delegate* unmanaged[Cdecl]<Handle, double*, ulong, int>)Entry(10))(
                pen, pointer, (ulong)values.Length));
    }

    internal static Handle FontCreate(string family, float size,
                                      global::System.Drawing.FontStyle style,
                                      global::System.Drawing.GraphicsUnit unit,
                                      byte charset)
    {
        if (FacadeCallTelemetry.IsEnabled)
        {
            FacadeCallTelemetry.Observe("font.family", family);
            FacadeCallTelemetry.Observe("font.spec",
                family + "|" + size.ToString(global::System.Globalization.CultureInfo.InvariantCulture) +
                "|" + style + "|" + unit + "|" + charset);
        }
        var bytes = Encoding.UTF8.GetBytes(family);
        fixed (byte* pointer = bytes)
        {
            Handle handle;
            Check(((delegate* unmanaged[Cdecl]<StringView, double, uint, uint, uint, Handle*, int>)Entry(11))(
                new StringView { Data = pointer, Size = (ulong)bytes.Length }, size,
                (uint)style, (uint)unit, charset, &handle));
            return handle;
        }
    }

    internal static float FontPixelSize(float size,
                                        global::System.Drawing.GraphicsUnit unit) => unit switch
    {
        global::System.Drawing.GraphicsUnit.Display => size * 96f / 75f,
        global::System.Drawing.GraphicsUnit.Point => size * 96f / 72f,
        global::System.Drawing.GraphicsUnit.Inch => size * 96f,
        global::System.Drawing.GraphicsUnit.Document => size * 96f / 300f,
        global::System.Drawing.GraphicsUnit.Millimeter => size * 96f / 25.4f,
        _ => size,
    };

    internal static global::System.Drawing.SizeF MeasureString(
        string text, global::System.Drawing.Font font,
        global::System.Drawing.StringFormat? format, int layoutWidth)
    {
        EnsureRaster();
        var bytes = Encoding.UTF8.GetBytes(text);
        NativeSize measured;
        var started = FacadeCallTelemetry.IsEnabled ?
            global::System.Diagnostics.Stopwatch.GetTimestamp() : 0L;
        fixed (byte* pointer = bytes)
            Check(((delegate* unmanaged[Cdecl]<StringView, Handle, Handle, double, NativeSize*, int>)Entry(96))(
                new StringView { Data = pointer, Size = (ulong)bytes.Length },
                font.__handle, format?.__handle ?? default, layoutWidth, &measured));
        if (FacadeCallTelemetry.IsEnabled)
        {
            var elapsed = global::System.Diagnostics.Stopwatch.GetTimestamp() - started;
            FacadeCallTelemetry.ObserveValue("text-measure.nanoseconds",
                elapsed * 1_000_000_000L / global::System.Diagnostics.Stopwatch.Frequency);
            FacadeCallTelemetry.ObserveValue("text-measure.utf8-bytes", bytes.Length);
        }
        return new global::System.Drawing.SizeF((float)measured.Width,
                                                (float)measured.Height);
    }

    internal static Handle StringFormatCreate(uint flags)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<uint, Handle*, int>)Entry(12))(flags, &handle));
        return handle;
    }
    internal static void StringFormatSet(Handle format,
        global::System.Drawing.StringAlignment alignment,
        global::System.Drawing.StringAlignment lineAlignment,
        global::System.Drawing.StringTrimming trimming,
        global::System.Drawing.StringFormatFlags flags) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, uint, uint, uint, uint, int>)Entry(13))(
            format, (uint)alignment, (uint)lineAlignment, (uint)trimming, (uint)flags));

    internal static Handle RecorderCreate()
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<Handle*, int>)Entry(14))(&handle));
        return handle;
    }
    internal static ulong RecorderSave(Handle recorder)
    {
        ulong token;
        Check(((delegate* unmanaged[Cdecl]<Handle, ulong*, int>)Entry(15))(recorder, &token));
        return token;
    }
    internal static void RecorderRestore(Handle recorder, ulong token) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, ulong, int>)Entry(16))(recorder, token));
    internal static void RecorderTranslate(Handle recorder, float x, float y) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, double, double, int>)Entry(17))(recorder, x, y));
    internal static void RecorderSetTransform(Handle recorder,
        global::System.Drawing.Drawing2D.Matrix matrix) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, MatrixValue, int>)Entry(18))(
            recorder, new MatrixValue { M11 = matrix.__m11, M12 = matrix.__m12,
                M21 = matrix.__m21, M22 = matrix.__m22,
                Dx = matrix.__dx, Dy = matrix.__dy }));
    internal static void RecorderSetClip(Handle recorder, global::System.Drawing.RectangleF rect) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Rect, int>)Entry(19))(recorder, Native(rect)));
    internal static bool RecorderIsVisible(Handle recorder, global::System.Drawing.PointF point)
    {
        uint visible;
        Check(((delegate* unmanaged[Cdecl]<Handle, Point, uint*, int>)Entry(22))(
            recorder, new Point { X = point.X, Y = point.Y }, &visible));
        return visible != 0;
    }
    internal static void RecorderClear(Handle recorder, global::System.Drawing.Color color) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, NativeColor, int>)Entry(23))(recorder, Native(color)));
    internal static void FillRectangle(Handle recorder, Handle brush, global::System.Drawing.RectangleF rect) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, Rect, int>)Entry(24))(recorder, brush, Native(rect)));
    internal static void DrawRectangle(Handle recorder, Handle pen, global::System.Drawing.RectangleF rect) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, Rect, int>)Entry(25))(recorder, pen, Native(rect)));
    internal static void DrawLine(Handle recorder, Handle pen,
                                  global::System.Drawing.PointF first,
                                  global::System.Drawing.PointF second) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, Point, Point, int>)Entry(26))(
            recorder, pen, new Point { X = first.X, Y = first.Y },
            new Point { X = second.X, Y = second.Y }));
    internal static void DrawString(Handle recorder, string text,
                                    global::System.Drawing.Font font,
                                    global::System.Drawing.Brush brush,
                                    global::System.Drawing.PointF origin,
                                    global::System.Drawing.StringFormat? format)
    {
        if (string.IsNullOrEmpty(text)) return;
        var temporary = format is null;
        format ??= new global::System.Drawing.StringFormat();
        try
        {
            var bytes = Encoding.UTF8.GetBytes(text);
            fixed (byte* pointer = bytes)
                Check(((delegate* unmanaged[Cdecl]<Handle, StringView, Handle, Handle, Point, Handle, int>)Entry(27))(
                    recorder, new StringView { Data = pointer, Size = (ulong)bytes.Length },
                    font.__handle, brush.__handle,
                    new Point { X = origin.X, Y = origin.Y }, format.__handle));
        }
        finally { if (temporary) format.Dispose(); }
    }
    internal static void RecorderQuality(global::System.Drawing.Graphics graphics)
    {
        graphics.__EnsureRecorder();
        static uint Interpolation(global::System.Drawing.Drawing2D.InterpolationMode value) => value switch
        {
            global::System.Drawing.Drawing2D.InterpolationMode.NearestNeighbor => 3,
            global::System.Drawing.Drawing2D.InterpolationMode.Bilinear => 4,
            global::System.Drawing.Drawing2D.InterpolationMode.Bicubic => 5,
            global::System.Drawing.Drawing2D.InterpolationMode.HighQualityBilinear => 6,
            global::System.Drawing.Drawing2D.InterpolationMode.HighQualityBicubic => 7,
            _ => (uint)global::System.Math.Clamp((int)value, 0, 2),
        };
        Check(((delegate* unmanaged[Cdecl]<Handle, uint, uint, uint, uint, uint, int>)Entry(21))(
            graphics.__recorder,
            (uint)global::System.Math.Clamp((int)graphics.__smoothing, 0, 4),
            Interpolation(graphics.__interpolation),
            (uint)global::System.Math.Clamp((int)graphics.__pixelOffset, 0, 4),
            (uint)global::System.Math.Clamp((int)graphics.__compositing, 0, 1),
            (uint)global::System.Math.Clamp((int)graphics.__compositingQuality, 0, 4)));
    }
    internal static void DrawEllipse(Handle recorder, Handle pen, global::System.Drawing.RectangleF rect) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, Rect, int>)Entry(43))(recorder, pen, Native(rect)));
    internal static void FillEllipse(Handle recorder, Handle brush, global::System.Drawing.RectangleF rect) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, Rect, int>)Entry(44))(recorder, brush, Native(rect)));

    internal static void DrawImage(Handle recorder, global::System.Drawing.Image image,
                                   global::System.Drawing.RectangleF destination,
                                   global::System.Drawing.RectangleF source,
                                   global::System.Drawing.Imaging.ImageAttributes? attributes)
    {
        if (image is null) throw new global::System.ArgumentNullException(nameof(image));
        image.__FlushGraphics();
        TraceImageContent(image);
        var temporary = attributes is null;
        attributes ??= new global::System.Drawing.Imaging.ImageAttributes();
        try
        {
            Check(((delegate* unmanaged[Cdecl]<Handle, Handle, Rect, Rect, Handle, int>)Entry(59))(
                recorder, image.__BitmapHandle, Native(destination), Native(source), attributes.__handle));
        }
        finally { if (temporary) attributes.Dispose(); }
    }

    private static void TraceImageContent(global::System.Drawing.Image image)
    {
        if (!traceImageContent || image.Width <= 0 || image.Height <= 0) return;
        var dimensions = image.Width.ToString(global::System.Globalization.CultureInfo.InvariantCulture) +
            "x" + image.Height.ToString(global::System.Globalization.CultureInfo.InvariantCulture);
        var sequence = tracedImageDimensions.AddOrUpdate(dimensions, 1, static (_, prior) => prior + 1);
        if (sequence > 3) return;
        var data = BitmapLock(image.__BitmapHandle,
            global::System.Drawing.Imaging.ImageLockMode.ReadOnly);
        try
        {
            var pixels = (byte*)data.Scan0;
            var total = checked((long)image.Width * image.Height);
            var step = global::System.Math.Max(1,
                (int)global::System.Math.Sqrt(global::System.Math.Max(1d, total / 4096d)));
            var minB = 255; var minG = 255; var minR = 255; var minA = 255;
            var maxB = 0; var maxG = 0; var maxR = 0; var maxA = 0;
            long samples = 0; long differing = 0;
            uint first = 0; var haveFirst = false;
            for (var y = 0; y < image.Height; y += step)
            {
                var row = pixels + checked(y * data.Stride);
                for (var x = 0; x < image.Width; x += step)
                {
                    var pixel = row + checked(x * 4);
                    var b = pixel[0]; var g = pixel[1]; var r = pixel[2]; var a = pixel[3];
                    minB = global::System.Math.Min(minB, b); maxB = global::System.Math.Max(maxB, b);
                    minG = global::System.Math.Min(minG, g); maxG = global::System.Math.Max(maxG, g);
                    minR = global::System.Math.Min(minR, r); maxR = global::System.Math.Max(maxR, r);
                    minA = global::System.Math.Min(minA, a); maxA = global::System.Math.Max(maxA, a);
                    var packed = (uint)(b | g << 8 | r << 16 | a << 24);
                    if (!haveFirst) { first = packed; haveFirst = true; }
                    else if (packed != first) ++differing;
                    ++samples;
                }
            }
            global::System.Console.Error.WriteLine("gui-drawing-image-content=dimensions:" +
                dimensions + "|sequence:" + sequence.ToString(global::System.Globalization.CultureInfo.InvariantCulture) +
                "|b:" + minB + "-" + maxB + "|g:" + minG + "-" + maxG +
                "|r:" + minR + "-" + maxR + "|a:" + minA + "-" + maxA +
                "|different:" + differing + "/" + samples);
        }
        finally { BitmapUnlock(image.__BitmapHandle, data); }
    }
    internal static global::System.Drawing.Imaging.BitmapData BitmapLock(
        Handle bitmap, global::System.Drawing.Imaging.ImageLockMode mode)
    {
        var nativeMode = mode switch
        {
            global::System.Drawing.Imaging.ImageLockMode.ReadOnly => 0u,
            global::System.Drawing.Imaging.ImageLockMode.WriteOnly => 1u,
            global::System.Drawing.Imaging.ImageLockMode.ReadWrite => 2u,
            _ => throw new ArgumentOutOfRangeException(nameof(mode)),
        };
        BitmapLockView view;
        Check(((delegate* unmanaged[Cdecl]<Handle, uint, BitmapLockView*, int>)Entry(53))(
            bitmap, nativeMode, &view));
        return new global::System.Drawing.Imaging.BitmapData
        {
            __bitmap = bitmap, __token = view.Token,
            __scan0 = (nint)(view.WritableData != null ? view.WritableData : view.Data),
            __stride = checked((int)view.RowBytes),
            __nativeScan0 = (nint)(view.WritableData != null ? view.WritableData : view.Data),
            __nativeStride = checked((int)view.RowBytes),
            __lockWidth = checked((int)view.Width), __lockHeight = checked((int)view.Height),
        };
    }
    internal static global::System.Drawing.Imaging.BitmapData BitmapLock(
        Handle bitmap, global::System.Drawing.Rectangle rectangle,
        global::System.Drawing.Imaging.ImageLockMode mode,
        global::System.Drawing.Imaging.PixelFormat format)
    {
        var data = BitmapLock(bitmap, mode);
        try
        {
            if (rectangle.X < 0 || rectangle.Y < 0 || rectangle.Width <= 0 || rectangle.Height <= 0 ||
                rectangle.Right > data.__lockWidth || rectangle.Bottom > data.__lockHeight)
                throw new global::System.ArgumentOutOfRangeException(nameof(rectangle));
            data.__lockX = rectangle.X; data.__lockY = rectangle.Y;
            data.__lockWidth = rectangle.Width; data.__lockHeight = rectangle.Height;
            if (format == global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb)
            {
                data.__scan0 = data.__nativeScan0 + rectangle.Y * data.__nativeStride + rectangle.X * 4;
                return data;
            }
            if (format != global::System.Drawing.Imaging.PixelFormat.Format32bppArgb)
                throw new global::System.NotSupportedException("GUI.Drawing bitmap locks currently admit 32-bit ARGB and premultiplied ARGB formats.");
            data.__stride = checked(rectangle.Width * 4);
            data.__staging = new byte[checked(data.__stride * rectangle.Height)];
            if (mode != global::System.Drawing.Imaging.ImageLockMode.WriteOnly)
            {
                fixed (byte* targetStart = data.__staging)
                {
                    var sourceStart = (byte*)data.__nativeScan0 + rectangle.Y * data.__nativeStride + rectangle.X * 4;
                    for (var y = 0; y < rectangle.Height; ++y)
                    {
                        var source = sourceStart + y * data.__nativeStride;
                        var target = targetStart + y * data.__stride;
                        for (var x = 0; x < rectangle.Width; ++x)
                        {
                            var alpha = source[x * 4 + 3];
                            target[x * 4] = alpha == 0 ? (byte)0 : (byte)global::System.Math.Min(255, (source[x * 4] * 255 + alpha / 2) / alpha);
                            target[x * 4 + 1] = alpha == 0 ? (byte)0 : (byte)global::System.Math.Min(255, (source[x * 4 + 1] * 255 + alpha / 2) / alpha);
                            target[x * 4 + 2] = alpha == 0 ? (byte)0 : (byte)global::System.Math.Min(255, (source[x * 4 + 2] * 255 + alpha / 2) / alpha);
                            target[x * 4 + 3] = alpha;
                        }
                    }
                }
            }
            data.__stagingPin = global::System.Runtime.InteropServices.GCHandle.Alloc(
                data.__staging, global::System.Runtime.InteropServices.GCHandleType.Pinned);
            data.__scan0 = data.__stagingPin.AddrOfPinnedObject();
            data.__writeBackStraightAlpha = mode != global::System.Drawing.Imaging.ImageLockMode.ReadOnly;
            return data;
        }
        catch
        {
            BitmapUnlock(bitmap, data);
            throw;
        }
    }
    internal static void BitmapUnlock(Handle bitmap,
                                      global::System.Drawing.Imaging.BitmapData data)
    {
        if (data.__bitmap != bitmap || data.__token == 0)
            throw new ArgumentException("BitmapData does not belong to this bitmap.", nameof(data));
        try
        {
            if (data.__writeBackStraightAlpha && data.__staging is not null)
            {
                fixed (byte* sourceStart = data.__staging)
                {
                    var targetStart = (byte*)data.__nativeScan0 + data.__lockY * data.__nativeStride + data.__lockX * 4;
                    for (var y = 0; y < data.__lockHeight; ++y)
                    {
                        var source = sourceStart + y * data.__stride;
                        var target = targetStart + y * data.__nativeStride;
                        for (var x = 0; x < data.__lockWidth; ++x)
                        {
                            var alpha = source[x * 4 + 3];
                            target[x * 4] = (byte)((source[x * 4] * alpha + 127) / 255);
                            target[x * 4 + 1] = (byte)((source[x * 4 + 1] * alpha + 127) / 255);
                            target[x * 4 + 2] = (byte)((source[x * 4 + 2] * alpha + 127) / 255);
                            target[x * 4 + 3] = alpha;
                        }
                    }
                }
            }
        }
        finally
        {
            if (data.__stagingPin.IsAllocated) data.__stagingPin.Free();
            Check(((delegate* unmanaged[Cdecl]<Handle, ulong, int>)Entry(54))(bitmap, data.__token));
            data.__token = 0;
            data.__scan0 = 0;
            data.__nativeScan0 = 0;
            data.__staging = null;
        }
    }

    internal static Handle GraphicsPathCreate()
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<uint, Handle*, int>)Entry(31))(0, &handle));
        return handle;
    }
    internal static void GraphicsPathReset(Handle path) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, int>)Entry(32))(path));
    internal static void GraphicsPathStart(Handle path) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, int>)Entry(33))(path));
    internal static void GraphicsPathClose(Handle path) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, int>)Entry(34))(path));
    internal static void GraphicsPathAddLine(Handle path,
        global::System.Drawing.PointF first, global::System.Drawing.PointF second) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Point, Point, int>)Entry(35))(
            path, new Point { X = first.X, Y = first.Y },
            new Point { X = second.X, Y = second.Y }));
    internal static void GraphicsPathAddRectangle(Handle path,
        global::System.Drawing.RectangleF rect) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Rect, int>)Entry(36))(path, Native(rect)));
    internal static void GraphicsPathAddEllipse(Handle path,
        global::System.Drawing.RectangleF rect) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Rect, int>)Entry(37))(path, Native(rect)));
    internal static global::System.Drawing.RectangleF GraphicsPathBounds(Handle path)
    {
        Rect bounds;
        Check(((delegate* unmanaged[Cdecl]<Handle, Rect*, int>)Entry(38))(path, &bounds));
        return new((float)bounds.X, (float)bounds.Y, (float)bounds.Width, (float)bounds.Height);
    }
    internal static void GraphicsPathAddArc(Handle path,
        global::System.Drawing.RectangleF rect, float start, float sweep) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Rect, double, double, int>)Entry(60))(
            path, Native(rect), start, sweep));
    internal static void GraphicsPathAddPath(Handle path, Handle added, bool connect) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, uint, int>)Entry(61))(
            path, added, connect ? 1u : 0u));
    internal static void GraphicsPathTransform(Handle path,
        global::System.Drawing.Drawing2D.Matrix matrix) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, MatrixValue, int>)Entry(62))(
            path, new MatrixValue { M11 = matrix.__m11, M12 = matrix.__m12,
                M21 = matrix.__m21, M22 = matrix.__m22,
                Dx = matrix.__dx, Dy = matrix.__dy }));
    internal static bool GraphicsPathIsVisible(Handle path,
        global::System.Drawing.PointF point)
    {
        uint visible;
        Check(((delegate* unmanaged[Cdecl]<Handle, Point, uint*, int>)Entry(63))(
            path, new Point { X = point.X, Y = point.Y }, &visible));
        return visible != 0;
    }
    internal static global::System.Drawing.PointF[] GraphicsPathPoints(Handle path)
    {
        ulong required;
        var first = ((delegate* unmanaged[Cdecl]<Handle, Point*, ulong, ulong*, int>)Entry(64))(
            path, null, 0, &required);
        if (required == 0 && first == 0) return [];
        if (first != 6) Check(first);
        var native = new Point[checked((int)required)];
        fixed (Point* pointer = native)
            Check(((delegate* unmanaged[Cdecl]<Handle, Point*, ulong, ulong*, int>)Entry(64))(
                path, pointer, (ulong)native.Length, &required));
        var result = new global::System.Drawing.PointF[native.Length];
        for (var index = 0; index < result.Length; ++index)
            result[index] = new((float)native[index].X, (float)native[index].Y);
        return result;
    }
    internal static global::System.Drawing.Drawing2D.GraphicsPath GraphicsPathClone(Handle path)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle*, int>)Entry(65))(path, &handle));
        var result = new global::System.Drawing.Drawing2D.GraphicsPath();
        var replaced = result.__handle;
        Release(ref replaced);
        result.__handle = handle;
        return result;
    }

    internal static Handle HatchBrushCreate(uint style, global::System.Drawing.Color foreground,
                                            global::System.Drawing.Color background)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<uint, NativeColor, NativeColor, Handle*, int>)Entry(68))(
            style, Native(foreground), Native(background), &handle));
        return handle;
    }
    internal static Handle LinearGradientBrushCreate(global::System.Drawing.RectangleF bounds,
        global::System.Drawing.Color first, global::System.Drawing.Color second,
        float angle, global::System.Drawing.Drawing2D.WrapMode wrap)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<Rect, NativeColor, NativeColor, double, uint, Handle*, int>)Entry(69))(
            Native(bounds), Native(first), Native(second), angle, (uint)wrap, &handle));
        return handle;
    }
    internal static void LinearGradientSetInterpolation(Handle brush,
        global::System.Drawing.Color[] colors, float[] positions)
    {
        if (colors is null || positions is null || colors.Length != positions.Length)
            throw new ArgumentException("Gradient colors and positions must have equal lengths.");
        var nativeColors = new NativeColor[colors.Length];
        var nativePositions = new double[positions.Length];
        for (var index = 0; index < colors.Length; ++index)
        { nativeColors[index] = Native(colors[index]); nativePositions[index] = positions[index]; }
        fixed (NativeColor* colorPointer = nativeColors)
        fixed (double* positionPointer = nativePositions)
            Check(((delegate* unmanaged[Cdecl]<Handle, NativeColor*, double*, ulong, int>)Entry(71))(
                brush, colorPointer, positionPointer, (ulong)colors.Length));
    }
    internal static void LinearGradientSetWrap(Handle brush,
        global::System.Drawing.Drawing2D.WrapMode wrap) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)Entry(72))(brush, (uint)wrap));
    internal static Handle PathGradientBrushCreate(global::System.Drawing.PointF[] points)
    {
        var native = new Point[points.Length];
        for (var index = 0; index < points.Length; ++index)
            native[index] = new Point { X = points[index].X, Y = points[index].Y };
        fixed (Point* pointer = native)
        {
            Handle handle;
            Check(((delegate* unmanaged[Cdecl]<Point*, ulong, uint, Handle*, int>)Entry(73))(
                pointer, (ulong)native.Length, 4, &handle));
            return handle;
        }
    }
    internal static void PathGradientSetCenterColor(Handle brush,
        global::System.Drawing.Color color) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, NativeColor, int>)Entry(74))(brush, Native(color)));
    internal static void PathGradientSetSurroundColors(Handle brush,
        global::System.Drawing.Color[] colors)
    {
        if (colors is null) throw new ArgumentNullException(nameof(colors));
        var native = new NativeColor[colors.Length];
        for (var index = 0; index < colors.Length; ++index) native[index] = Native(colors[index]);
        fixed (NativeColor* pointer = native)
            Check(((delegate* unmanaged[Cdecl]<Handle, NativeColor*, ulong, int>)Entry(76))(
                brush, pointer, (ulong)native.Length));
    }

    internal static Handle RegionCreate(global::System.Drawing.RectangleF rectangle)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<Rect, Handle*, int>)Entry(78))(Native(rectangle), &handle));
        return handle;
    }
    internal static Handle RegionCreate(Handle path)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle*, int>)Entry(79))(path, &handle));
        return handle;
    }
    internal static void RegionUnion(Handle region, global::System.Drawing.RectangleF rectangle) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Rect, int>)Entry(80))(region, Native(rectangle)));
    internal static void RegionUnion(Handle region, Handle path) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, int>)Entry(81))(region, path));
    internal static void RegionExclude(Handle region, global::System.Drawing.RectangleF rectangle) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Rect, int>)Entry(82))(region, Native(rectangle)));
    internal static global::System.Drawing.RectangleF RegionBounds(Handle region)
    {
        Rect bounds;
        Check(((delegate* unmanaged[Cdecl]<Handle, Rect*, int>)Entry(84))(region, &bounds));
        return new global::System.Drawing.RectangleF((float)bounds.X, (float)bounds.Y,
            (float)bounds.Width, (float)bounds.Height);
    }

    internal static Handle ImageAttributesCreate()
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<Handle*, int>)Entry(40))(&handle));
        return handle;
    }
    internal static void ImageAttributesSetColorMatrix(Handle attributes, float[] values)
    {
        if (values is null || values.Length != 25) throw new ArgumentException("Color matrix requires 25 values.");
        var native = new double[25];
        for (var index = 0; index < native.Length; ++index) native[index] = values[index];
        fixed (double* pointer = native)
            Check(((delegate* unmanaged[Cdecl]<Handle, double*, ulong, int>)Entry(41))(
                attributes, pointer, 25));
    }
    internal static void ImageAttributesSetRemap(Handle attributes,
        global::System.Drawing.Imaging.ColorMap[] maps)
    {
        if (maps is null) throw new ArgumentNullException(nameof(maps));
        var native = new ColorRemap[maps.Length];
        for (var index = 0; index < maps.Length; ++index)
            native[index] = new ColorRemap { OldColor = Native(maps[index].__oldColor),
                                             NewColor = Native(maps[index].__newColor) };
        fixed (ColorRemap* pointer = native)
            Check(((delegate* unmanaged[Cdecl]<Handle, ColorRemap*, ulong, int>)Entry(66))(
                attributes, pointer, (ulong)native.Length));
    }
    internal static global::System.Drawing.Imaging.ImageAttributes ImageAttributesClone(Handle attributes)
    {
        Handle handle;
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle*, int>)Entry(89))(attributes, &handle));
        var result = new global::System.Drawing.Imaging.ImageAttributes();
        Release(ref result.__handle);
        result.__handle = handle;
        return result;
    }

    internal static void FillPolygon(Handle recorder, Handle brush,
                                     global::System.Drawing.PointF[] points)
    {
        var native = new Point[points.Length];
        for (var index = 0; index < points.Length; ++index)
            native[index] = new Point { X = points[index].X, Y = points[index].Y };
        fixed (Point* pointer = native)
            Check(((delegate* unmanaged[Cdecl]<Handle, Handle, Point*, ulong, uint, int>)Entry(45))(
                recorder, brush, pointer, (ulong)native.Length, 0));
    }
    internal static void DrawPath(Handle recorder, Handle pen, Handle path) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, Handle, int>)Entry(46))(
            recorder, pen, path));
    internal static void FillPath(Handle recorder, Handle brush, Handle path) =>
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, Handle, int>)Entry(47))(
            recorder, brush, path));

    internal static void Execute(Handle recorder, Handle bitmap)
    {
        var recorded = RecorderCommandCount(recorder);
        if (recorded == 0) return;
        EnsureRaster();
        var started = FacadeCallTelemetry.IsEnabled ? global::System.Diagnostics.Stopwatch.GetTimestamp() : 0L;
        ulong count;
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, ulong*, int>)Entry(86))(
            recorder, bitmap, &count));
        if (FacadeCallTelemetry.IsEnabled)
        {
            FacadeCallTelemetry.ObserveValue("raster.command-count", checked((long)count));
            FacadeCallTelemetry.ObserveValue("raster.execute-nanoseconds",
                checked(global::System.Diagnostics.Stopwatch.GetElapsedTime(started).Ticks * 100L));
        }
    }

    internal static ulong RecorderCommandCount(Handle recorder)
    {
        ulong count;
        Check(((delegate* unmanaged[Cdecl]<Handle, ulong*, int>)Entry(29))(
            recorder, &count));
        return count;
    }

    internal static ulong ExecuteFrom(Handle recorder, Handle bitmap, ulong firstCommand)
    {
        EnsureRaster();
        var started = FacadeCallTelemetry.IsEnabled ?
            global::System.Diagnostics.Stopwatch.GetTimestamp() : 0L;
        ulong count;
        Check(((delegate* unmanaged[Cdecl]<Handle, Handle, ulong, ulong*, int>)Entry(97))(
            recorder, bitmap, firstCommand, &count));
        if (FacadeCallTelemetry.IsEnabled && count != 0)
        {
            FacadeCallTelemetry.ObserveValue("raster.incremental-command-count",
                checked((long)count));
            FacadeCallTelemetry.ObserveValue("raster.incremental-execute-nanoseconds",
                checked(global::System.Diagnostics.Stopwatch.GetElapsedTime(started).Ticks * 100L));
        }
        return count;
    }

    internal static void Flush(global::System.Drawing.Graphics graphics)
    {
        var target = graphics.__target;
        var recorder = graphics.__recorder;
        if (target is null || recorder.IsNull) return;
        if (graphics.__executedCommands == RecorderCommandCount(recorder)) return;
        var telemetry = FacadeCallTelemetry.IsEnabled;
        var flushStarted = telemetry ?
            global::System.Diagnostics.Stopwatch.GetTimestamp() : 0L;
        // A long-lived Graphics.FromHdc/FromHwnd may be interleaved with raw
        // GDI writes to the same borrowed surface.  Its private raster is only
        // a command candidate, not an authoritative copy of that surface.
        // Refresh it immediately before applying the newly recorded commands
        // so an external BitBlt/Clear completed since the previous Flush is
        // observed exactly once instead of being overwritten by stale pixels.
        if (graphics.__nativeSurface != 0)
        {
            var refreshStarted = telemetry ?
                global::System.Diagnostics.Stopwatch.GetTimestamp() : 0L;
            RefreshNativeSurfaceTarget(graphics);
            if (telemetry)
                FacadeCallTelemetry.ObserveValue("native-surface.refresh-nanoseconds",
                    checked(global::System.Diagnostics.Stopwatch.GetElapsedTime(refreshStarted).Ticks * 100L));
        }
        var executeStarted = telemetry ?
            global::System.Diagnostics.Stopwatch.GetTimestamp() : 0L;
        graphics.__executedCommands += ExecuteFrom(
            recorder, target.__BitmapHandle,
            graphics.__executedCommands);
        if (telemetry)
            FacadeCallTelemetry.ObserveValue("native-surface.flush-execute-nanoseconds",
                checked(global::System.Diagnostics.Stopwatch.GetElapsedTime(executeStarted).Ticks * 100L));
        if (graphics.__nativeSurface != 0)
        {
            var presentStarted = telemetry ?
                global::System.Diagnostics.Stopwatch.GetTimestamp() : 0L;
            PresentNativeSurface(graphics);
            if (telemetry)
                FacadeCallTelemetry.ObserveValue("native-surface.present-nanoseconds",
                    checked(global::System.Diagnostics.Stopwatch.GetElapsedTime(presentStarted).Ticks * 100L));
        }
        CompactExecutedRecorder(graphics);
        if (telemetry)
            FacadeCallTelemetry.ObserveValue("native-surface.flush-total-nanoseconds",
                checked(global::System.Diagnostics.Stopwatch.GetElapsedTime(flushStarted).Ticks * 100L));
    }

    // Graphics.FromHwnd is commonly retained for the lifetime of a custom
    // control.  Executed commands are already committed to its bitmap, so
    // retaining them forever only consumes memory and eventually reaches the
    // native recorder's hard command ceiling.  Rebase at a frame boundary and
    // seed a fresh recorder with the managed drawing state required by future
    // commands.  An open Save/Restore scope is deliberately left intact.
    private const ulong ExecutedRecorderCompactionThreshold = 4096;
    private static void CompactExecutedRecorder(global::System.Drawing.Graphics graphics)
    {
        if (graphics.__savedStateDepth != 0 ||
            graphics.__executedCommands < ExecutedRecorderCompactionThreshold)
            return;
        var replacement = RecorderCreate();
        var previous = graphics.__recorder;
        graphics.__recorder = replacement;
        try
        {
            RecorderSetTransform(replacement, graphics.__transform);
            if (graphics.__nativeOriginX != 0f || graphics.__nativeOriginY != 0f)
                RecorderTranslate(replacement, graphics.__nativeOriginX,
                    graphics.__nativeOriginY);
            if (graphics.__hasClip) RecorderSetClip(replacement, graphics.__clip);
            RecorderQuality(graphics);
            graphics.__executedCommands = RecorderCommandCount(replacement);
        }
        catch
        {
            Release(ref replacement);
            graphics.__recorder = previous;
            throw;
        }
        Release(ref previous);
    }

    internal static byte[] EncodePng(Handle bitmap)
    {
        EnsureRaster();
        ulong required;
        var first = ((delegate* unmanaged[Cdecl]<Handle, void*, ulong, ulong*, int>)Entry(87))(
            bitmap, null, 0, &required);
        if (first != 6) Check(first);
        var bytes = new byte[checked((int)required)];
        fixed (byte* pointer = bytes)
            Check(((delegate* unmanaged[Cdecl]<Handle, void*, ulong, ulong*, int>)Entry(87))(
                bitmap, pointer, (ulong)bytes.Length, &required));
        if (FacadeCallTelemetry.IsEnabled)
        {
            Dimensions(bitmap, out var width, out var height);
            FacadeCallTelemetry.Observe("image.encode-format", "png");
            FacadeCallTelemetry.Observe("image.encode-dimensions", width.ToString(global::System.Globalization.CultureInfo.InvariantCulture) + "x" + height.ToString(global::System.Globalization.CultureInfo.InvariantCulture));
            FacadeCallTelemetry.ObserveValue("image.encoded-bytes", bytes.Length);
        }
        return bytes;
    }

    internal static global::System.Drawing.Bitmap DecodePng(byte[] bytes)
    {
        EnsureRaster();
        if (bytes is null || bytes.Length == 0) throw new ArgumentException("PNG data is empty.", nameof(bytes));
        fixed (byte* pointer = bytes)
        {
            Handle handle;
            Check(((delegate* unmanaged[Cdecl]<void*, ulong, Handle*, int>)Entry(88))(
                pointer, (ulong)bytes.Length, &handle));
            Dimensions(handle, out var width, out var height);
            FacadeCallTelemetry.Observe("image.decode-format", "png");
            FacadeCallTelemetry.Observe("image.decode-dimensions", width.ToString(global::System.Globalization.CultureInfo.InvariantCulture) + "x" + height.ToString(global::System.Globalization.CultureInfo.InvariantCulture));
            FacadeCallTelemetry.ObserveValue("image.decoded-input-bytes", bytes.Length);
            var bitmap = WrapBitmap(handle, width, height);
            // PNG exposes straight-alpha ARGB semantics even though the owned
            // raster store normalizes its internal bytes to premultiplied BGRA.
            bitmap.__pixelFormat = global::System.Drawing.Imaging.PixelFormat.Format32bppArgb;
            return bitmap;
        }
    }
    internal static byte[] ReadBounded(global::System.IO.Stream stream, string format)
    {
        const int maximumBytes = 64 * 1024 * 1024;
        using var copy = new global::System.IO.MemoryStream();
        var buffer = new byte[81920];
        while (true)
        {
            var read = stream.Read(buffer, 0, buffer.Length);
            if (read == 0) break;
            if (copy.Length + read > maximumBytes)
                throw new ArgumentException(format + " data exceeds the 64 MiB compatibility limit.", nameof(stream));
            copy.Write(buffer, 0, read);
        }
        return copy.ToArray();
    }
    internal static global::System.Drawing.Bitmap DecodeIcon(byte[] bytes)
    {
        if (bytes is null || bytes.Length < 22 || bytes[0] != 0 || bytes[1] != 0 ||
            bytes[2] != 1 || bytes[3] != 0)
            throw new ArgumentException("ICO directory is invalid.", nameof(bytes));
        static ushort U16(byte[] data, int offset) =>
            (ushort)(data[offset] | data[offset + 1] << 8);
        static uint U32(byte[] data, int offset) =>
            (uint)(data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 |
                   data[offset + 3] << 24);
        var count = U16(bytes, 4);
        if (count == 0 || 6 + count * 16 > bytes.Length)
            throw new ArgumentException("ICO directory is truncated.", nameof(bytes));
        var selectedOffset = -1;
        var selectedSize = 0;
        var selectedScore = -1L;
        for (var index = 0; index < count; ++index)
        {
            var entry = 6 + index * 16;
            var size = checked((int)U32(bytes, entry + 8));
            var offset = checked((int)U32(bytes, entry + 12));
            if (size < 8 || offset < 0 || offset > bytes.Length - size) continue;
            var width = bytes[entry] == 0 ? 256 : bytes[entry];
            var height = bytes[entry + 1] == 0 ? 256 : bytes[entry + 1];
            var score = (long)width * height * 65536 + U16(bytes, entry + 6);
            if (score <= selectedScore) continue;
            selectedScore = score;
            selectedOffset = offset;
            selectedSize = size;
        }
        if (selectedOffset < 0)
            throw new ArgumentException("ICO contains no bounded image frame.", nameof(bytes));
        if (selectedSize >= 8 && bytes[selectedOffset] == 0x89 && bytes[selectedOffset + 1] == 0x50 &&
            bytes[selectedOffset + 2] == 0x4e && bytes[selectedOffset + 3] == 0x47 &&
            bytes[selectedOffset + 4] == 0x0d && bytes[selectedOffset + 5] == 0x0a &&
            bytes[selectedOffset + 6] == 0x1a && bytes[selectedOffset + 7] == 0x0a)
            return DecodePng(bytes.AsSpan(selectedOffset, selectedSize).ToArray());
        return DecodeIconDib(bytes, selectedOffset, selectedSize);
    }
    private static global::System.Drawing.Bitmap DecodeIconDib(byte[] bytes, int frameOffset, int frameSize)
    {
        static ushort U16(byte[] data, int offset) =>
            (ushort)(data[offset] | data[offset + 1] << 8);
        static uint U32(byte[] data, int offset) =>
            (uint)(data[offset] | data[offset + 1] << 8 | data[offset + 2] << 16 |
                   data[offset + 3] << 24);
        if (frameSize < 40 || U32(bytes, frameOffset) < 40)
            throw new NotSupportedException("ICO DIB header is unsupported.");
        var width = checked((int)U32(bytes, frameOffset + 4));
        var storedHeight = checked((int)U32(bytes, frameOffset + 8));
        var planes = U16(bytes, frameOffset + 12);
        var bitsPerPixel = U16(bytes, frameOffset + 14);
        var compression = U32(bytes, frameOffset + 16);
        if (width <= 0 || width > 4096 || storedHeight <= 0 || storedHeight > 8192 ||
            (storedHeight & 1) != 0 || planes != 1 ||
            bitsPerPixel != 32 || compression != 0)
            throw new NotSupportedException("GUI.Drawing admits uncompressed 32-bit ICO DIB frames.");
        var height = storedHeight / 2;
        var headerSize = checked((int)U32(bytes, frameOffset));
        var sourceStride = checked(width * 4);
        var xorBytes = checked(sourceStride * height);
        if (headerSize > frameSize || xorBytes > frameSize - headerSize)
            throw new ArgumentException("ICO DIB pixels are truncated.", nameof(bytes));
        var sourceOffset = frameOffset + headerSize;
        var anyAlpha = false;
        for (var index = 3; index < xorBytes; index += 4)
            anyAlpha |= bytes[sourceOffset + index] != 0;
        var maskStride = checked(((width + 31) / 32) * 4);
        var maskOffset = sourceOffset + xorBytes;
        var hasMask = maskStride <= frameSize - headerSize - xorBytes &&
                      checked(maskStride * height) <= frameSize - headerSize - xorBytes;
        var bitmap = new global::System.Drawing.Bitmap(width, height,
            global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
        var locked = bitmap.LockBits(new global::System.Drawing.Rectangle(0, 0, width, height),
            global::System.Drawing.Imaging.ImageLockMode.WriteOnly,
            global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb);
        try
        {
            for (var y = 0; y < height; ++y)
            {
                var sourceRow = sourceOffset + (height - 1 - y) * sourceStride;
                var maskRow = maskOffset + (height - 1 - y) * maskStride;
                var destination = (byte*)locked.Scan0 + y * locked.Stride;
                for (var x = 0; x < width; ++x)
                {
                    var source = sourceRow + x * 4;
                    var alpha = anyAlpha ? bytes[source + 3] :
                        hasMask && (bytes[maskRow + x / 8] & (0x80 >> (x & 7))) != 0 ? (byte)0 : (byte)255;
                    destination[x * 4] = (byte)((bytes[source] * alpha + 127) / 255);
                    destination[x * 4 + 1] = (byte)((bytes[source + 1] * alpha + 127) / 255);
                    destination[x * 4 + 2] = (byte)((bytes[source + 2] * alpha + 127) / 255);
                    destination[x * 4 + 3] = alpha;
                }
            }
        }
        finally { bitmap.UnlockBits(locked); }
        return bitmap;
    }
    private static global::System.Drawing.Bitmap WrapBitmap(Handle handle, int width, int height)
    {
        var bitmap = new global::System.Drawing.Bitmap();
        bitmap.__bitmap = handle;
        bitmap.__width = width;
        bitmap.__height = height;
        bitmap.__pixelFormat = global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb;
        return bitmap;
    }

    internal static global::System.Drawing.Graphics GraphicsFromNativeSurface(nint surface,
                                                                               uint kind)
    {
        if (surface == 0) throw new ArgumentException("Native surface must be nonzero.", nameof(surface));
        var traceOwnership = global::System.Environment.GetEnvironmentVariable(
            "GUI_FORMS_TRACE_DIRECT_GDI_OWNERSHIP") == "1";
        if (traceOwnership)
            global::System.Console.Error.WriteLine("gui-drawing-direct-gdi=from-native-begin|surface:0x" +
                ((nuint)surface).ToString("x") + "|kind:" + kind + "|thread:" +
                global::System.Environment.CurrentManagedThreadId);
        Handle bitmapHandle;
        Rect bounds;
        var result = ((delegate* unmanaged[Cdecl]<nuint, uint, Handle*, Rect*, int>)Entry(92))(
            (nuint)surface, kind, &bitmapHandle, &bounds);
        if (traceOwnership)
            global::System.Console.Error.WriteLine("gui-drawing-direct-gdi=from-native-import|surface:0x" +
                ((nuint)surface).ToString("x") + "|kind:" + kind + "|result:" + result +
                "|bitmap:" + bitmapHandle.Slot + ":" + bitmapHandle.Generation + "|bounds:" +
                bounds.X + "," + bounds.Y + "," + bounds.Width + "x" + bounds.Height +
                "|thread:" + global::System.Environment.CurrentManagedThreadId);
        if (result == 7) throw new PlatformNotSupportedException(
            "Native HDC/HWND drawing is available only through the Windows adapter.");
        Check(result);
        Dimensions(bitmapHandle, out var width, out var height);
        if (traceOwnership)
            global::System.Console.Error.WriteLine("gui-drawing-direct-gdi=from-native-ready|surface:0x" +
                ((nuint)surface).ToString("x") + "|kind:" + kind + "|bitmap:" + bitmapHandle.Slot +
                ":" + bitmapHandle.Generation + "|size:" + width + "x" + height + "|thread:" +
                global::System.Environment.CurrentManagedThreadId);
        if (global::System.Environment.GetEnvironmentVariable("GUI_DRAWING_TRACE_NATIVE_SURFACES") == "1" &&
            global::System.Threading.Interlocked.Increment(ref nativeSurfaceCaptureTraceCount) <= 32)
            global::System.Console.Error.WriteLine("gui-drawing-native-surface=capture|kind:" + kind +
                "|size:" + width + "x" + height + "|thread:" + global::System.Environment.CurrentManagedThreadId);
        FacadeCallTelemetry.Observe("graphics.target-kind", kind == 0 ? "hdc" : "hwnd");
        FacadeCallTelemetry.Observe("graphics.target-dimensions", width.ToString(global::System.Globalization.CultureInfo.InvariantCulture) + "x" + height.ToString(global::System.Globalization.CultureInfo.InvariantCulture));
        var graphics = new global::System.Drawing.Graphics
        {
            __target = WrapBitmap(bitmapHandle, width, height),
            __nativeSurface = surface,
            __nativeSurfaceKind = kind,
        };
        graphics.__target.__AttachGraphics(graphics);
        graphics.__EnsureRecorder();
        if (bounds.X != 0 || bounds.Y != 0)
        {
            graphics.__nativeOriginX = (float)-bounds.X;
            graphics.__nativeOriginY = (float)-bounds.Y;
            RecorderTranslate(graphics.__recorder, graphics.__nativeOriginX,
                graphics.__nativeOriginY);
        }
        return graphics;
    }
    private static void RefreshNativeSurfaceTarget(global::System.Drawing.Graphics graphics)
    {
        var target = graphics.__target;
        if (target is null || graphics.__nativeSurface == 0) return;
        Rect bounds;
        var inPlace = ((delegate* unmanaged[Cdecl]<nuint, uint, Handle, Rect*, int>)Entry(102))(
            (nuint)graphics.__nativeSurface, graphics.__nativeSurfaceKind,
            target.__BitmapHandle, &bounds);
        if (inPlace == 0) return;
        // A native surface may be resized independently of its long-lived
        // Graphics. Reallocate only for that uncommon topology transition.
        if (inPlace != 1) Check(inPlace);
        Handle refreshed;
        var result = ((delegate* unmanaged[Cdecl]<nuint, uint, Handle*, Rect*, int>)Entry(92))(
            (nuint)graphics.__nativeSurface, graphics.__nativeSurfaceKind,
            &refreshed, &bounds);
        if (result == 7) throw new PlatformNotSupportedException(
            "Native surface refresh is available only through the Windows adapter.");
        Check(result);
        Dimensions(refreshed, out var width, out var height);
        var prior = target.__BitmapHandle;
        target.__bitmap = refreshed;
        target.__width = width;
        target.__height = height;
        target.__pixelFormat = global::System.Drawing.Imaging.PixelFormat.Format32bppPArgb;
        Release(ref prior);
    }
    internal static void PresentNativeSurface(global::System.Drawing.Graphics graphics)
    {
        if (graphics.__target is null || graphics.__nativeSurface == 0) return;
        var result = ((delegate* unmanaged[Cdecl]<nuint, uint, Handle, int>)Entry(93))(
            (nuint)graphics.__nativeSurface, graphics.__nativeSurfaceKind,
            graphics.__target.__BitmapHandle);
        if (result == 7) throw new PlatformNotSupportedException(
            "Native surface presentation is available only through the Windows adapter.");
        Check(result);
        var retained = ((delegate* unmanaged[Cdecl]<nuint, uint, Handle, int>)Entry(103))(
            (nuint)graphics.__nativeSurface, graphics.__nativeSurfaceKind,
            graphics.__target.__BitmapHandle);
        if (retained == 0) return;
        // A direct-GDI control is sampled into the retained tree only after its
        // managed Graphics flush has completed. This explicit boundary prevents
        // the polling fallback from publishing the intermediate Clear/BitBlt
        // states that otherwise appear as white flashes or split frames.
        if (global::System.OperatingSystem.IsWindows())
        {
            var window = graphics.__nativeSurfaceKind == 0
                ? WindowFromDC(graphics.__nativeSurface)
                : graphics.__nativeSurface;
            if (window != 0) _ = SendMessageW(window, NativeSurfacePresentedMessage, 0, 0);
        }
        if (global::System.Environment.GetEnvironmentVariable("GUI_DRAWING_TRACE_NATIVE_SURFACES") == "1" &&
            global::System.Threading.Interlocked.Increment(ref nativeSurfacePresentTraceCount) <= 64)
            global::System.Console.Error.WriteLine("gui-drawing-native-surface=present|kind:" +
                graphics.__nativeSurfaceKind + "|size:" + graphics.__target.Width + "x" +
                graphics.__target.Height + "|thread:" + global::System.Environment.CurrentManagedThreadId);
    }
    internal static nint GetHdc(global::System.Drawing.Graphics graphics) =>
        AcquireHdc(graphics);
    private static nint AcquireHdc(global::System.Drawing.Graphics graphics)
    {
        if (graphics.__target is null)
            throw new InvalidOperationException("Graphics has no bitmap-backed target.");
        graphics.__EnsureRecorder();
        if (graphics.__hdcLeaseToken != 0)
            throw new InvalidOperationException("An HDC lease is already active.");
        Execute(graphics.__recorder, graphics.__target.__BitmapHandle);
        var priorRecorder = graphics.__recorder;
        Release(ref priorRecorder);
        graphics.__recorder = RecorderCreate();
        graphics.__executedCommands = 0;
        nuint device;
        ulong token;
        var result = ((delegate* unmanaged[Cdecl]<Handle, nuint*, ulong*, int>)Entry(94))(
            graphics.__target.__BitmapHandle, &device, &token);
        if (result == 7) throw new PlatformNotSupportedException(
            "Bitmap-backed HDC leases are available only through the Windows adapter.");
        Check(result);
        graphics.__leasedHdc = (nint)device;
        graphics.__hdcLeaseToken = token;
        return graphics.__leasedHdc;
    }
    internal static void ReleaseHdc(global::System.Drawing.Graphics graphics, nint device)
    {
        if (graphics.__target is null || graphics.__hdcLeaseToken == 0 ||
            device == 0 || device != graphics.__leasedHdc)
            throw new ArgumentException("HDC does not belong to this Graphics lease.", nameof(device));
        var result = ((delegate* unmanaged[Cdecl]<Handle, ulong, int>)Entry(95))(
            graphics.__target.__BitmapHandle, graphics.__hdcLeaseToken);
        if (result == 7) throw new PlatformNotSupportedException(
            "Bitmap-backed HDC leases are available only through the Windows adapter.");
        Check(result);
        graphics.__leasedHdc = 0;
        graphics.__hdcLeaseToken = 0;
        if (graphics.__nativeSurface != 0) PresentNativeSurface(graphics);
    }
    internal static nint GetHbitmap(global::System.Drawing.Bitmap bitmap,
                                    global::System.Drawing.Color background)
    {
        nuint native;
        var result = ((delegate* unmanaged[Cdecl]<Handle, NativeColor, nuint*, int>)Entry(90))(
            bitmap.__BitmapHandle, Native(background), &native);
        if (result == 7) throw new PlatformNotSupportedException(
            "Native HBITMAP export is available only through the Windows adapter.");
        Check(result);
        return (nint)native;
    }
    internal static global::System.Drawing.Bitmap FromHbitmap(nint hbitmap)
    {
        if (hbitmap == 0) throw new ArgumentException("HBITMAP must be nonzero.", nameof(hbitmap));
        Handle handle;
        var result = ((delegate* unmanaged[Cdecl]<nuint, Handle*, int>)Entry(91))(
            (nuint)hbitmap, &handle);
        if (result == 7) throw new PlatformNotSupportedException(
            "Native HBITMAP import is available only through the Windows adapter.");
        Check(result);
        Dimensions(handle, out var width, out var height);
        return WrapBitmap(handle, width, height);
    }
}
""";

static string NativeBridgeSource() => """
// <auto-generated/>
#nullable enable
using System.Runtime.InteropServices;
using System.Runtime.CompilerServices;
using System.Linq;
using System.Text;
using System.Threading;

namespace System.Windows.Forms;

internal enum NativeChange { None, Name, Text, Visible, Enabled, Bounds, Tree }
internal enum NativeEvent : uint { Clicked = 2, FormClosing = 3, FormClosed = 4, RangeValueChanged = 14, RangeScroll = 15, BoundsChanged = 16, Scroll = 17 }
internal readonly record struct NativePointer(uint Kind, double X, double Y, double WheelDelta, uint Button);
internal readonly record struct NativeKey(uint Kind, uint PhysicalKey, uint Modifiers, bool Repeat);
internal readonly record struct NativeFieldEdit(string Text, int Anchor, int Caret, bool Changed, bool CanUndo, bool CanRedo);
internal readonly record struct NativeScrollAxis(bool Enabled, bool Visible, int Minimum, int Maximum, int LargeChange, int SmallChange, int Value);
internal readonly record struct NativeScrollState(bool AutoScroll, global::System.Drawing.Point Position, global::System.Drawing.Size Margin, global::System.Drawing.Size MinimumContentSize, global::System.Drawing.Rectangle DisplayRectangle, global::System.Drawing.Rectangle ViewportRectangle, NativeScrollAxis Horizontal, NativeScrollAxis Vertical, ulong EventRevision, uint EventType, uint EventOrientation, int EventOldValue, int EventNewValue);
internal readonly record struct NativeLayoutState(uint SuspendDepth, bool Deferred, ulong RequestedRevision, ulong CommittedRevision);

internal sealed unsafe class NativeControlBridge : IDisposable
{
    [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
    private unsafe delegate uint DispatchThunk(void* context, uint cancelled);
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    private delegate nint WindowSurfaceThunk(nint window, uint message, nint wParam, nint lParam);
    private static readonly DispatchThunk dispatchThunk = DispatchCallback;
    private static readonly nint dispatchThunkPointer = Marshal.GetFunctionPointerForDelegate(dispatchThunk);
    private static readonly WindowSurfaceThunk windowSurfaceThunk = WindowSurfaceProcedure;
    private static readonly nint windowSurfaceThunkPointer = Marshal.GetFunctionPointerForDelegate(windowSurfaceThunk);
    [StructLayout(LayoutKind.Sequential)] internal struct Handle { internal uint Slot; internal uint Generation; }
    [StructLayout(LayoutKind.Sequential)] private struct StringView { internal byte* Data; internal ulong Size; }
    [StructLayout(LayoutKind.Sequential)] private struct ErrorView { internal uint Code; internal StringView Message; }
    [StructLayout(LayoutKind.Sequential)] private struct Rect { internal double X, Y, Width, Height; }
    [StructLayout(LayoutKind.Sequential)] private struct Point { internal double X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct Size { internal double Width, Height; }
    [StructLayout(LayoutKind.Sequential)] private struct ScrollAxisState { internal uint Enabled, Visible; internal double Minimum, Maximum, LargeChange, SmallChange, Value; }
    [StructLayout(LayoutKind.Sequential)] private struct ScrollState { internal uint AutoScroll; internal Point Position; internal Size Margin, MinimumContentSize; internal Rect DisplayRectangle, ViewportRectangle; internal ScrollAxisState Horizontal, Vertical; internal ulong EventRevision; internal uint EventType, EventOrientation; internal double EventOldValue, EventNewValue; }
    [StructLayout(LayoutKind.Sequential)] private struct LayoutState { internal uint SuspendDepth, Deferred; internal ulong RequestedRevision, CommittedRevision; }
    [StructLayout(LayoutKind.Sequential)] private struct FieldEditResult { internal ulong Anchor, Caret, Revision; internal uint Changed, CanUndo, CanRedo; }
    [StructLayout(LayoutKind.Sequential)] private struct PropertyValue { internal uint Kind, BooleanValue; internal long SignedValue; internal ulong UnsignedValue; internal double NumberValue; internal uint ColorArgb; internal StringView TextValue; }
    [StructLayout(LayoutKind.Sequential)] private struct PropertyEnumChoice { internal StringView Name; internal long Value; }
    [StructLayout(LayoutKind.Sequential)] private struct PropertyDescriptorV1 { internal uint StructSize, Kind, Flags, Reserved; internal StringView Name, Category, Description, EnumTypeName; internal PropertyEnumChoice* EnumChoices; internal ulong EnumChoiceCount; internal PropertyValue* StandardValues; internal ulong StandardValueCount; internal StringView ConverterName, EditorName; }
    [StructLayout(LayoutKind.Sequential)] private struct PropertyCallbacksV1 { internal uint StructSize, Reserved; internal void* Context; internal nint Get, Set, Reset, ShouldSerialize, Format, Parse, Edit; }
    [StructLayout(LayoutKind.Sequential)] private struct BitmapInfoHeader { internal uint Size; internal int Width, Height; internal ushort Planes, BitCount; internal uint Compression, SizeImage; internal int XPelsPerMeter, YPelsPerMeter; internal uint ClrUsed, ClrImportant; }
    [StructLayout(LayoutKind.Sequential)] private struct BitmapInfo { internal BitmapInfoHeader Header; internal uint Color; }
    [StructLayout(LayoutKind.Sequential)] private struct PaintStruct { internal nint Device; internal int Erase; internal int Left, Top, Right, Bottom; internal int Restore, IncUpdate; internal fixed byte Reserved[32]; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct WindowClass { internal uint Style; internal nint WindowProcedure; internal int ClassExtra, WindowExtra; internal nint Instance, Icon, Cursor, Background; internal string? MenuName; internal string ClassName; }
    [StructLayout(LayoutKind.Sequential)] private struct Api
    {
        internal uint StructSize, AbiVersion;
        internal nint LastError, ControlCreate, Retain, Release, Dispose, ComponentState, StableId;
        internal nint SetVisible, GetVisible, SetBounds, GetBounds, AddChild, RemoveChild, Subscribe, Disconnect;
        internal nint ControlCreateKind, SetName, GetName, SetText, GetText, SetEnabled, GetEnabled;
        internal nint RunWindow, LastHostTrace;
        internal nint SubscribeV2, BeginInvoke, RequestClose, CallbackFaultCount;
        internal nint SetControlPng, SetChildIndex, SetControlColors, SubscribePointer;
        internal nint SetCheckState, GetCheckState;
        internal nint SubscribeKey, SubscribeText;
        internal nint SetRange, GetRange, SetRangeValue, GetRangeValue;
        internal nint SetPointerCapture, GetPointerCapture;
        internal nint ShowPathDialog, LastDialogPath, ShowTooltip, HideTooltip;
        internal nint SetFieldSelection, SetFieldEditState, FieldPositionFromPoint;
        internal nint WriteClipboardText, ReadClipboardText;
        internal nint FieldNavigate;
        internal nint FieldReplace, FieldHistory, FieldClearHistory;
        internal nint SetControlPixels;
        internal nint GetControlAbsoluteBounds;
        internal nint SubscribeKeyPreview;
        internal nint SetCursor, GetCursor;
        internal nint SetAutoScrollOffset, SetAutoScroll, SetAutoScrollMargin;
        internal nint SetAutoScrollMinSize, SetAutoScrollPosition, GetScrollState;
        internal nint SetScrollAxisState, ScrollControlIntoView;
        internal nint SuspendLayout, ResumeLayout, PerformControlLayout, GetLayoutState;
        internal nint PropertyGridSetSelectedControls, PropertyGridSetSort;
        internal nint PropertyGridGetSort, PropertyGridRefresh;
        internal nint PropertyObjectDefine, PropertyObjectNotifyChanged;
        internal nint PropertyGridTrySetText, PropertyGridResetProperty;
        internal nint PropertyGridActivateEditor;
        internal nint SetControlTextAlignment, SetButtonAppearance;
        internal nint SetPanelBorderStyle;
        internal nint AttachOverlay, DetachOverlay;
    }

    [DllImport("gui_forms_abi0", EntryPoint = "gf_get_api_v0", CallingConvention = CallingConvention.Cdecl)]
    private static extern int GetApi(uint requestedVersion, ref Api api);
    [DllImport("gui_forms_abi0", EntryPoint = "gf_windows_paint_endpoint_acquire_v1", CallingConvention = CallingConvention.Cdecl)]
    private static extern int AcquireWindowsPaintEndpoint(Handle control, uint width, uint height, ulong* endpoint, nint* window);
    [DllImport("gui_forms_abi0", EntryPoint = "gf_windows_paint_endpoint_configure_v1", CallingConvention = CallingConvention.Cdecl)]
    private static extern int ConfigureWindowsPaintEndpoint(ulong endpoint, uint width, uint height);
    [DllImport("gui_forms_abi0", EntryPoint = "gf_windows_paint_endpoint_touch_v1", CallingConvention = CallingConvention.Cdecl)]
    private static extern int TouchWindowsPaintEndpoint(ulong endpoint, uint explicitBoundary);
    [DllImport("gui_forms_abi0", EntryPoint = "gf_windows_paint_endpoint_drain_v1", CallingConvention = CallingConvention.Cdecl)]
    private static extern int DrainWindowsPaintEndpoint(ulong endpoint);
    [DllImport("gui_forms_abi0", EntryPoint = "gf_windows_paint_endpoint_snapshot_v1", CallingConvention = CallingConvention.Cdecl)]
    private static extern int SnapshotWindowsPaintEndpoint(ulong endpoint, byte* buffer, ulong capacity, ulong* requiredSize);
    [DllImport("gui_forms_abi0", EntryPoint = "gf_windows_paint_endpoint_release_v1", CallingConvention = CallingConvention.Cdecl)]
    private static extern int ReleaseWindowsPaintEndpoint(ulong endpoint);
    private static readonly Api api = LoadApi();
    private static readonly bool traceControls = Environment.GetEnvironmentVariable("GUI_FORMS_TRACE_CONTROLS") == "1";
    private static readonly bool traceDelegates = Environment.GetEnvironmentVariable("GUI_FORMS_TRACE_DELEGATES") == "1";
    private static readonly bool traceDirectGdiOwnership = Environment.GetEnvironmentVariable("GUI_FORMS_TRACE_DIRECT_GDI_OWNERSHIP") == "1";
    private const string windowSurfaceClassName = "GUIForms.ControlSurface.v1";
    private static readonly string[] directWindowSurfaceTypes = (Environment.GetEnvironmentVariable("GUI_FORMS_DIRECT_HWND_TYPES") ?? string.Empty)
        .Split(';', global::System.StringSplitOptions.RemoveEmptyEntries | global::System.StringSplitOptions.TrimEntries);
    private static readonly global::System.Collections.Concurrent.ConcurrentDictionary<global::System.Reflection.Assembly, byte>
        directGdiAssemblies = new();
    private static nint directGdiShim;
    private static readonly bool windowSurfaceClassRegistered = RegisterWindowSurfaceClass();
    private static long nextId;
    private static long nextAsyncId;
    private static long nextSurfaceId;
    [ThreadStatic] private static int nativeCallbackDepth;
    internal static bool InNativeCallback => nativeCallbackDepth != 0;
    private static readonly global::System.Collections.Concurrent.ConcurrentDictionary<int,
        global::System.Collections.Concurrent.ConcurrentDictionary<long, NativeAsyncResult>> pendingByThread = new();
    private static readonly global::System.Collections.Concurrent.ConcurrentDictionary<long,
        global::System.WeakReference<NativeControlBridge>> windowSurfaces = new();
    private static readonly global::System.Collections.Concurrent.ConcurrentDictionary<nint,
        global::System.WeakReference<NativeControlBridge>> windowSurfacesByHandle = new();
    [ThreadStatic] private static bool flushingWindowSurfaces;

    private static void ExitNativeCallback()
    {
        if (--nativeCallbackDepth != 0) return;
        // Commit every custom-painted control touched by one native input
        // callback before the host can present its retained tree. This keeps a
        // WinForms event's related text, bounds, and raster changes atomic and
        // prevents one-frame fragments from old control surfaces.
        global::System.Windows.Forms.Control.__FlushCallbackPaintQueue();
        FlushWindowSurfaces(global::System.Environment.CurrentManagedThreadId);
    }
    private SafeControlHandle handle;
    private readonly string stableId;
    private readonly string managedTypeName;
    private readonly bool supportsRaster;
    private readonly bool exposesWindowSurface;
    private readonly bool promotesPointerClick;
    private GCHandle callbackRoot;
    private readonly global::System.Collections.Generic.List<Handle> subscriptions = new();
    private readonly int ownerThreadId;
    private readonly long surfaceId = Interlocked.Increment(ref nextSurfaceId);
    private readonly object windowHandleGate = new();
    private readonly object stateGate = new();
    private nint windowHandle;
    private ulong nativePaintEndpoint;
    private nint windowParent;
    private global::System.Drawing.Point windowPosition;
    private global::System.Drawing.Size windowSize;
    private global::System.Drawing.Point appliedWindowPosition;
    private global::System.Drawing.Size appliedWindowSize;
    private bool appliedWindowVisible;
    private bool haveAppliedWindowState;
    private bool windowSurfaceConfigured;
    private global::System.Threading.Timer? windowSurfaceTimer;
    private const int WindowSurfaceMinimumProbeMilliseconds = 33;
    private const int WindowSurfaceMaximumProbeMilliseconds = 250;
    private int windowSurfaceProbeIntervalMilliseconds = WindowSurfaceMinimumProbeMilliseconds;
    private volatile bool hasExplicitPresentBoundary;
    private int windowSurfaceDrainQueued;
    private int windowSurfaceDrainActive;
    private int windowSurfaceDirtyAfterDrain;
    private int windowSurfaceProbePending;
    private long windowSurfaceContentRevision;
    private long windowSurfaceCapturedRevision;
    private long windowSurfaceEpoch = 1;
    private long windowSurfaceDrainsQueued;
    private long windowSurfaceDrainsCoalesced;
    private long windowSurfaceDrainsStarted;
    private long windowSurfaceDrainsCommitted;
    private long windowSurfaceDrainsUnchanged;
    private nint captureDevice;
    private nint captureBitmap;
    private nint capturePreviousBitmap;
    private byte* capturePixels;
    private int captureWidth;
    private int captureHeight;
    private int windowSurfaceTraceCount;
    private ulong windowSurfaceContentHash;
    private int windowSurfaceContentTraceCount;
    private string cachedName = string.Empty;
    private string cachedText = string.Empty;
    private volatile bool cachedVisible = true;
    private volatile bool cachedEnabled = true;
    private global::System.Drawing.Rectangle cachedBounds;
    private int boundsSynchronizationPending;
    internal bool IsDisposed { get; private set; }
    // A retained ABI handle is an identity token, not an HWND. Returning it to
    // Win32 callers causes GetDC/SendMessage to operate on an invalid window.
    internal bool HasWindowHandle => windowHandle != 0;
    internal nint WindowHandle
    {
        get
        {
            if (IsDisposed || !global::System.OperatingSystem.IsWindows()) return 0;
            lock (windowHandleGate)
            {
                if (windowHandle == 0)
                {
                    var width = global::System.Math.Max(1, cachedBounds.Width);
                    var height = global::System.Math.Max(1, cachedBounds.Height);
                    if (exposesWindowSurface)
                    {
                        nint compatibilityHandle;
                        ulong endpoint;
                        Check(AcquireWindowsPaintEndpoint(handle.Value,
                            checked((uint)width), checked((uint)height),
                            &endpoint, &compatibilityHandle));
                        nativePaintEndpoint = endpoint;
                        windowHandle = compatibilityHandle;
                        if (traceDirectGdiOwnership)
                            global::System.Console.Error.WriteLine("gui-forms-direct-gdi=acquire|surface:" + surfaceId +
                                "|control:" + stableId + "|type:" + managedTypeName + "|endpoint:" + endpoint +
                                "|virtual-handle:0x" + ((nuint)compatibilityHandle).ToString("x") + "|size:" + width + "x" + height +
                                "|thread:" + global::System.Environment.CurrentManagedThreadId);
                    }
                    else
                    {
                        if (!windowSurfaceClassRegistered) return 0;
                        // Ordinary compatibility handles remain identity/message
                        // endpoints. Direct-GDI controls take the native-host path
                        // above and never install this managed WndProc.
                        windowHandle = CreateWindowExW(0x08000080u, windowSurfaceClassName, string.Empty, 0x80000000u,
                            0, 0, width, height, 0, 0, 0, 0);
                    }
                    if (windowHandle != 0 && !exposesWindowSurface)
                    {
                        _ = EnableWindow(windowHandle, false);
                        windowSurfaces[surfaceId] = new(this);
                        windowSurfacesByHandle[windowHandle] = new(this);
                    }
                }
                return windowHandle;
            }
        }
    }
    internal event Action<NativeChange>? Changed;
    internal event Func<NativeEvent, bool>? NativeEventRaised;
    internal event Action<NativePointer>? PointerRaised;
    internal event Action<NativeKey>? KeyRaised;
    internal event Func<NativeKey, bool>? KeyPreviewRaised;
    internal event Action<string, bool, int, int>? TextRaised;

    private NativeControlBridge(SafeControlHandle handle, uint kind, string stableId, string managedTypeName, bool scrollable,
                                global::System.Drawing.Size initialSize)
    {
        this.handle = handle;
        this.stableId = stableId;
        this.managedTypeName = managedTypeName;
        supportsRaster = kind is 20u or 23u or 0x7fffffffu;
        exposesWindowSurface = global::System.Array.Exists(directWindowSurfaceTypes,
            candidate => global::System.String.Equals(candidate, managedTypeName, global::System.StringComparison.Ordinal));
        promotesPointerClick = kind is 4u or 5u or 11u or 14u;
        ownerThreadId = Environment.CurrentManagedThreadId;
        cachedBounds = new global::System.Drawing.Rectangle(
            global::System.Drawing.Point.Empty, initialSize);
        callbackRoot = GCHandle.Alloc(this, GCHandleType.Weak);
        if (kind is 4u or 5u or 11u or 14u) SubscribeTyped(NativeEvent.Clicked);
        if (kind == 1u) { SubscribeTyped(NativeEvent.FormClosing); SubscribeTyped(NativeEvent.FormClosed); SubscribeTyped(NativeEvent.BoundsChanged); }
        if (kind == 10u) { SubscribeTyped(NativeEvent.RangeScroll); SubscribeTyped(NativeEvent.RangeValueChanged); }
        if (scrollable) SubscribeTyped(NativeEvent.Scroll);
        SubscribePointer();
        if (kind == 1u) SubscribeKeyPreview();
        if (kind is 6u or 8u or 9u or 16u or 18u) { SubscribeKey(); SubscribeText(); }
        else if (kind == 0x7fffffffu) SubscribeKey();
        // A WinForms-compatible Handle is owned by the thread which created the
        // control, not by whichever render worker happens to ask for it first.
        // Direct-GDI controls therefore establish their native paint lease while
        // the base Control constructor still runs on the owning UI thread.
        if (exposesWindowSurface && global::System.OperatingSystem.IsWindows())
            _ = WindowHandle;
    }

    internal bool PromotesPointerClick => promotesPointerClick;

    private static void ConfigureDirectGdiImports(global::System.Reflection.Assembly assembly)
    {
        if (!global::System.OperatingSystem.IsWindows() ||
            !directGdiAssemblies.TryAdd(assembly, 0)) return;
        try
        {
            NativeLibrary.SetDllImportResolver(assembly, (libraryName, owner, searchPath) =>
            {
                if (!global::System.String.Equals(libraryName, "user32.dll", global::System.StringComparison.OrdinalIgnoreCase) &&
                    !global::System.String.Equals(libraryName, "gdi32.dll", global::System.StringComparison.OrdinalIgnoreCase))
                    return 0;
                var loaded = global::System.Threading.Volatile.Read(ref directGdiShim);
                if (loaded != 0) return loaded;
                loaded = NativeLibrary.Load("gui_forms_win32_compat", owner, searchPath);
                global::System.Threading.Interlocked.CompareExchange(ref directGdiShim, loaded, 0);
                return global::System.Threading.Volatile.Read(ref directGdiShim);
            });
        }
        catch
        {
            directGdiAssemblies.TryRemove(assembly, out _);
            throw;
        }
    }

    internal static NativeControlBridge Create(Type managedType)
    {
        var stableId = $"forms.{managedType.Name}.{Interlocked.Increment(ref nextId)}";
        var directWindowSurface = global::System.Array.Exists(
            directWindowSurfaceTypes,
            candidate => global::System.String.Equals(candidate,
                managedType.FullName ?? managedType.Name,
                global::System.StringComparison.Ordinal));
        var paintMethod = managedType.GetMethod("OnPaint", global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic);
        var paintBackgroundMethod = managedType.GetMethod("OnPaintBackground", global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.NonPublic);
        var retainedField = typeof(ComboBox).IsAssignableFrom(managedType) ||
            typeof(NumericUpDown).IsAssignableFrom(managedType) ||
            typeof(TextBoxBase).IsAssignableFrom(managedType);
        var toolStripSurface = typeof(ToolStrip).IsAssignableFrom(managedType);
        var buttonSurface = typeof(Button).IsAssignableFrom(managedType);
        var transparentPaintSurface = (typeof(Label).IsAssignableFrom(managedType) &&
            !typeof(LinkLabel).IsAssignableFrom(managedType)) || typeof(PictureBox).IsAssignableFrom(managedType);
        var pictureBoxSurface = typeof(PictureBox).IsAssignableFrom(managedType);
        var customPaint = global::System.OperatingSystem.IsWindows() &&
            (toolStripSurface || buttonSurface || pictureBoxSurface || (!retainedField && !typeof(Form).IsAssignableFrom(managedType) &&
            (paintMethod?.DeclaringType?.Assembly != typeof(Control).Assembly ||
             paintBackgroundMethod?.DeclaringType?.Assembly != typeof(Control).Assembly)));
        // A direct-GDI endpoint is sampled by the retained compositor as a
        // RasterControl regardless of whether reflection can see an OnPaint
        // override. Handle creation happens in the bridge constructor, so this
        // backing choice must be complete before the bridge exists.
        var overlaySurface = typeof(ToolStripDropDown).IsAssignableFrom(managedType) ||
            managedType.Name == "HostedCaptionSurface";
        var kind = directWindowSurface ? 0x7fffffffu : overlaySurface ? 23u :
            customPaint && transparentPaintSurface ? 20u : customPaint ? 0x7fffffffu : 0u;
        for (var current = managedType; current is not null && kind == 0u; current = current.BaseType)
        {
            kind = current.Name switch
            {
                "Form" => 1u, "UserControl" => 2u,
                "ScrollableControl" or "ContainerControl" or "Panel" => 3u, "Button" => 4u,
                "CheckBox" => 5u, "ComboBox" => 6u, "Label" => 7u, "ListBox" => 8u,
                "TextBox" or "TextBoxBase" => 9u, "TrackBar" => 10u,
                "RadioButton" => 11u, "GroupBox" => 12u, "ProgressBar" => 13u,
                "LinkLabel" => 14u, "PictureBox" => 15u, "DataGridView" => 16u,
                "ToolStrip" => 17u, "NumericUpDown" => 18u,
                "PropertyGrid" => 21u, _ => 0u,
            };
        }
        var bytes = Encoding.UTF8.GetBytes(stableId);
        Handle value;
        fixed (byte* data = bytes)
            Check(((delegate* unmanaged[Cdecl]<uint, StringView, Handle*, int>)api.ControlCreateKind)(kind, new StringView { Data = data, Size = (ulong)bytes.Length }, &value));
        if (directWindowSurface)
            ConfigureDirectGdiImports(managedType.Assembly);
        if (traceControls) Console.Error.WriteLine($"facade-control=create|id={stableId}|type={managedType.FullName}|kind={kind}");
        // WinForms UserControl establishes a 150x150 client area before the
        // derived constructor runs. A compatibility consumer may legitimately
        // create HDC-backed bitmaps immediately after its base constructor returns.
        // The compatibility handle and retained raster therefore need the same
        // initial geometry; a later layout pass is too late.
        var initialSize = typeof(UserControl).IsAssignableFrom(managedType)
            ? new global::System.Drawing.Size(150, 150)
            : global::System.Drawing.Size.Empty;
        return new NativeControlBridge(new SafeControlHandle(value), kind, stableId,
            managedType.FullName ?? managedType.Name,
            typeof(ScrollableControl).IsAssignableFrom(managedType), initialSize);
    }

    private void EnsureAlive() { if (IsDisposed) throw new global::System.InvalidOperationException("GUI.Forms control is disposed."); }
    internal string Name { get { EnsureAlive(); lock (stateGate) return cachedName; } set => SetString(api.SetName, value, NativeChange.Name); }
    internal string Text { get { EnsureAlive(); lock (stateGate) return cachedText; } set => SetString(api.SetText, value, NativeChange.Text); }
    internal bool Visible { get { EnsureAlive(); return cachedVisible; } set => SetBool(api.SetVisible, value, NativeChange.Visible); }
    internal bool Enabled { get { EnsureAlive(); return cachedEnabled; } set => SetBool(api.SetEnabled, value, NativeChange.Enabled); }
    internal uint CursorKind
    {
        get { EnsureAlive(); uint value; Check(((delegate* unmanaged[Cdecl]<Handle, uint*, int>)api.GetCursor)(handle.Value, &value)); return value; }
        set { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.SetCursor)(handle.Value, value)); }
    }
    internal global::System.Drawing.Rectangle Bounds
    {
        get { EnsureAlive(); lock (stateGate) return cachedBounds; }
        set { lock (stateGate) cachedBounds = value; ResizeWindowSurface(value.Width, value.Height); Check(((delegate* unmanaged[Cdecl]<Handle, Rect, int>)api.SetBounds)(handle.Value, new Rect { X = value.X, Y = value.Y, Width = value.Width, Height = value.Height })); }
    }
    internal void SetProjectedBounds(global::System.Drawing.Rectangle value) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, Rect, int>)api.SetBounds)(handle.Value, new Rect { X = value.X, Y = value.Y, Width = value.Width, Height = value.Height })); }
    internal void SetProjectedVisible(bool value) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.SetVisible)(handle.Value, value ? 1u : 0u)); }
    internal void SetAutoScrollOffset(global::System.Drawing.Point value) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, Point, int>)api.SetAutoScrollOffset)(handle.Value, new Point { X = value.X, Y = value.Y })); }
    internal void SetAutoScroll(bool value) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.SetAutoScroll)(handle.Value, value ? 1u : 0u)); }
    internal void SetAutoScrollMargin(global::System.Drawing.Size value) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, Size, int>)api.SetAutoScrollMargin)(handle.Value, new Size { Width = value.Width, Height = value.Height })); }
    internal void SetAutoScrollMinSize(global::System.Drawing.Size value) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, Size, int>)api.SetAutoScrollMinSize)(handle.Value, new Size { Width = value.Width, Height = value.Height })); }
    internal void SetAutoScrollPosition(global::System.Drawing.Point value) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, Point, int>)api.SetAutoScrollPosition)(handle.Value, new Point { X = value.X, Y = value.Y })); }
    internal NativeScrollState GetScrollState() { EnsureAlive(); ScrollState value; Check(((delegate* unmanaged[Cdecl]<Handle, ScrollState*, int>)api.GetScrollState)(handle.Value, &value)); static NativeScrollAxis Axis(ScrollAxisState axis) => new(axis.Enabled != 0, axis.Visible != 0, (int)global::System.Math.Round(axis.Minimum), (int)global::System.Math.Round(axis.Maximum), (int)global::System.Math.Round(axis.LargeChange), (int)global::System.Math.Round(axis.SmallChange), (int)global::System.Math.Round(axis.Value)); return new(value.AutoScroll != 0, new global::System.Drawing.Point((int)global::System.Math.Round(value.Position.X), (int)global::System.Math.Round(value.Position.Y)), new global::System.Drawing.Size((int)global::System.Math.Round(value.Margin.Width), (int)global::System.Math.Round(value.Margin.Height)), new global::System.Drawing.Size((int)global::System.Math.Round(value.MinimumContentSize.Width), (int)global::System.Math.Round(value.MinimumContentSize.Height)), new global::System.Drawing.Rectangle((int)global::System.Math.Round(value.DisplayRectangle.X), (int)global::System.Math.Round(value.DisplayRectangle.Y), (int)global::System.Math.Round(value.DisplayRectangle.Width), (int)global::System.Math.Round(value.DisplayRectangle.Height)), new global::System.Drawing.Rectangle((int)global::System.Math.Round(value.ViewportRectangle.X), (int)global::System.Math.Round(value.ViewportRectangle.Y), (int)global::System.Math.Round(value.ViewportRectangle.Width), (int)global::System.Math.Round(value.ViewportRectangle.Height)), Axis(value.Horizontal), Axis(value.Vertical), value.EventRevision, value.EventType, value.EventOrientation, (int)global::System.Math.Round(value.EventOldValue), (int)global::System.Math.Round(value.EventNewValue)); }
    internal void SetScrollAxisState(bool vertical, NativeScrollAxis value) { EnsureAlive(); var state = new ScrollAxisState { Enabled = value.Enabled ? 1u : 0u, Visible = value.Visible ? 1u : 0u, Minimum = value.Minimum, Maximum = value.Maximum, LargeChange = value.LargeChange, SmallChange = value.SmallChange, Value = value.Value }; Check(((delegate* unmanaged[Cdecl]<Handle, uint, ScrollAxisState, int>)api.SetScrollAxisState)(handle.Value, vertical ? 1u : 0u, state)); }
    internal void ScrollControlIntoView(NativeControlBridge child) { EnsureAlive(); if (child is null) throw new global::System.ArgumentNullException(nameof(child)); Check(((delegate* unmanaged[Cdecl]<Handle, Handle, int>)api.ScrollControlIntoView)(handle.Value, child.handle.Value)); }
    internal void SuspendLayout() { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, int>)api.SuspendLayout)(handle.Value)); }
    internal void ResumeLayout(bool performLayout) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.ResumeLayout)(handle.Value, performLayout ? 1u : 0u)); }
    internal void PerformControlLayout() { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, int>)api.PerformControlLayout)(handle.Value)); }
    internal NativeLayoutState GetLayoutState() { EnsureAlive(); LayoutState value; Check(((delegate* unmanaged[Cdecl]<Handle, LayoutState*, int>)api.GetLayoutState)(handle.Value, &value)); return new(value.SuspendDepth, value.Deferred != 0, value.RequestedRevision, value.CommittedRevision); }
    internal PropertyObjectAdapter[] SetPropertyGridSelectedObjects(object[] objects, PropertyGrid ownerGrid) { EnsureAlive(); objects ??= global::System.Array.Empty<object>(); if (ownerGrid is null) throw new global::System.ArgumentNullException(nameof(ownerGrid)); var values = new Handle[objects.Length]; var adapters = new global::System.Collections.Generic.List<PropertyObjectAdapter>(); try { for (var index = 0; index < objects.Length; ++index) { var value = objects[index] ?? throw new global::System.ArgumentNullException(nameof(objects)); if (value is Control control) values[index] = control.__native.handle.Value; else { var adapter = new PropertyObjectAdapter(value, ownerGrid); adapters.Add(adapter); values[index] = adapter.Value; } } fixed (Handle* data = values) Check(((delegate* unmanaged[Cdecl]<Handle, Handle*, ulong, int>)api.PropertyGridSetSelectedControls)(handle.Value, data, (ulong)values.Length)); return adapters.ToArray(); } catch { foreach (var adapter in adapters) adapter.Dispose(); throw; } }
    internal bool TrySetPropertyGridText(string name, string value) { EnsureAlive(); var nameBytes = global::System.Text.Encoding.UTF8.GetBytes(name ?? throw new global::System.ArgumentNullException(nameof(name))); var valueBytes = global::System.Text.Encoding.UTF8.GetBytes(value ?? throw new global::System.ArgumentNullException(nameof(value))); uint committed; fixed (byte* nameData = nameBytes) fixed (byte* valueData = valueBytes) Check(((delegate* unmanaged[Cdecl]<Handle, StringView, StringView, uint*, int>)api.PropertyGridTrySetText)(handle.Value, new StringView { Data = nameData, Size = (ulong)nameBytes.Length }, new StringView { Data = valueData, Size = (ulong)valueBytes.Length }, &committed)); return committed != 0; }
    internal bool ResetPropertyGridProperty(string name) { EnsureAlive(); var bytes = global::System.Text.Encoding.UTF8.GetBytes(name ?? throw new global::System.ArgumentNullException(nameof(name))); uint committed; fixed (byte* data = bytes) Check(((delegate* unmanaged[Cdecl]<Handle, StringView, uint*, int>)api.PropertyGridResetProperty)(handle.Value, new StringView { Data = data, Size = (ulong)bytes.Length }, &committed)); return committed != 0; }
    internal bool ActivatePropertyGridEditor(string name) { EnsureAlive(); var bytes = global::System.Text.Encoding.UTF8.GetBytes(name ?? throw new global::System.ArgumentNullException(nameof(name))); uint activated; fixed (byte* data = bytes) Check(((delegate* unmanaged[Cdecl]<Handle, StringView, uint*, int>)api.PropertyGridActivateEditor)(handle.Value, new StringView { Data = data, Size = (ulong)bytes.Length }, &activated)); return activated != 0; }
    internal void SetPropertyGridSort(uint value) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.PropertyGridSetSort)(handle.Value, value)); }
    internal uint GetPropertyGridSort() { EnsureAlive(); uint value; Check(((delegate* unmanaged[Cdecl]<Handle, uint*, int>)api.PropertyGridGetSort)(handle.Value, &value)); return value; }
    internal void RefreshPropertyGrid() { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, int>)api.PropertyGridRefresh)(handle.Value)); }

    internal void AddChild(NativeControlBridge child) { if (traceControls) Console.Error.WriteLine($"facade-control=add|parent={stableId}|parent-type={managedTypeName}|child={child.stableId}|child-type={child.managedTypeName}"); Check(((delegate* unmanaged[Cdecl]<Handle, Handle, int>)api.AddChild)(handle.Value, child.handle.Value)); }
    internal void RemoveChild(NativeControlBridge child) { if (traceControls) Console.Error.WriteLine($"facade-control=remove|parent={stableId}|child={child.stableId}"); Check(((delegate* unmanaged[Cdecl]<Handle, Handle, int>)api.RemoveChild)(handle.Value, child.handle.Value)); }
    internal void AttachOverlay(NativeControlBridge owner) { if (owner is null) throw new global::System.ArgumentNullException(nameof(owner)); Check(((delegate* unmanaged[Cdecl]<Handle, Handle, int>)api.AttachOverlay)(owner.handle.Value, handle.Value)); }
    internal void DetachOverlay() { Check(((delegate* unmanaged[Cdecl]<Handle, int>)api.DetachOverlay)(handle.Value)); }
    internal void SetChildIndex(NativeControlBridge child, int index) { if (index < 0) throw new global::System.ArgumentOutOfRangeException(nameof(index)); Check(((delegate* unmanaged[Cdecl]<Handle, Handle, ulong, int>)api.SetChildIndex)(handle.Value, child.handle.Value, (ulong)index)); }
    internal void SetColors(global::System.Drawing.Color foreground, global::System.Drawing.Color background) { Check(((delegate* unmanaged[Cdecl]<Handle, uint, uint, int>)api.SetControlColors)(handle.Value, unchecked((uint)foreground.ToArgb()), unchecked((uint)background.ToArgb()))); }
    internal void SetTextAlignment(uint contentAlignment) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.SetControlTextAlignment)(handle.Value, contentAlignment)); }
    internal void SetButtonAppearance(uint visualStyle, double flatBorderWidth) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, double, int>)api.SetButtonAppearance)(handle.Value, visualStyle, flatBorderWidth)); }
    internal void SetPanelBorderStyle(uint borderStyle) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.SetPanelBorderStyle)(handle.Value, borderStyle)); }
    internal void SetFieldSelection(int start, int length, bool caretVisible) { EnsureAlive(); string text; lock (stateGate) text = cachedText; start = global::System.Math.Clamp(start, 0, text.Length); length = global::System.Math.Clamp(length, 0, text.Length - start); var startBytes = global::System.Text.Encoding.UTF8.GetByteCount(text.AsSpan(0, start)); var lengthBytes = global::System.Text.Encoding.UTF8.GetByteCount(text.AsSpan(start, length)); Check(((delegate* unmanaged[Cdecl]<Handle, ulong, ulong, uint, int>)api.SetFieldSelection)(handle.Value, (ulong)startBytes, (ulong)lengthBytes, caretVisible ? 1u : 0u)); }
    internal void SetFieldEditState(int anchor, int caret, bool caretVisible) { EnsureAlive(); string text; lock (stateGate) text = cachedText; anchor = global::System.Math.Clamp(anchor, 0, text.Length); caret = global::System.Math.Clamp(caret, 0, text.Length); var anchorBytes = global::System.Text.Encoding.UTF8.GetByteCount(text.AsSpan(0, anchor)); var caretBytes = global::System.Text.Encoding.UTF8.GetByteCount(text.AsSpan(0, caret)); Check(((delegate* unmanaged[Cdecl]<Handle, ulong, ulong, uint, int>)api.SetFieldEditState)(handle.Value, (ulong)anchorBytes, (ulong)caretBytes, caretVisible ? 1u : 0u)); }
    internal int FieldPositionFromPoint(double x) { EnsureAlive(); ulong bytePosition; Check(((delegate* unmanaged[Cdecl]<Handle, double, ulong*, int>)api.FieldPositionFromPoint)(handle.Value, x, &bytePosition)); string text; lock (stateGate) text = cachedText; var bytes = global::System.Text.Encoding.UTF8.GetBytes(text); var bounded = global::System.Math.Min(bytePosition, (ulong)bytes.Length); return global::System.Text.Encoding.UTF8.GetCharCount(bytes.AsSpan(0, checked((int)bounded))); }
    internal void WriteClipboardText(string text) { EnsureAlive(); var bytes = global::System.Text.Encoding.UTF8.GetBytes(text ?? string.Empty); fixed (byte* data = bytes) Check(((delegate* unmanaged[Cdecl]<Handle, StringView, int>)api.WriteClipboardText)(handle.Value, new StringView { Data = data, Size = (ulong)bytes.Length })); }
    internal string ReadClipboardText() { EnsureAlive(); ulong required; uint hasText; var result = ((delegate* unmanaged[Cdecl]<Handle, byte*, ulong, ulong*, uint*, int>)api.ReadClipboardText)(handle.Value, null, 0, &required, &hasText); if (result != 0 && result != 6) Check(result); if (hasText == 0 || required == 0) return string.Empty; if (required > 16UL * 1024UL * 1024UL) throw new global::System.InvalidOperationException("GUI.Forms clipboard text exceeds the managed boundary."); var bytes = new byte[checked((int)required)]; fixed (byte* data = bytes) Check(((delegate* unmanaged[Cdecl]<Handle, byte*, ulong, ulong*, uint*, int>)api.ReadClipboardText)(handle.Value, data, (ulong)bytes.Length, &required, &hasText)); return hasText == 0 ? string.Empty : global::System.Text.Encoding.UTF8.GetString(bytes); }
    internal int NavigateFieldPosition(int position, int direction) { EnsureAlive(); string text; lock (stateGate) text = cachedText; position = global::System.Math.Clamp(position, 0, text.Length); var positionBytes = global::System.Text.Encoding.UTF8.GetByteCount(text.AsSpan(0, position)); ulong resultBytes; Check(((delegate* unmanaged[Cdecl]<Handle, ulong, int, ulong*, int>)api.FieldNavigate)(handle.Value, (ulong)positionBytes, direction, &resultBytes)); var bytes = global::System.Text.Encoding.UTF8.GetBytes(text); var bounded = global::System.Math.Min(resultBytes, (ulong)bytes.Length); return global::System.Text.Encoding.UTF8.GetCharCount(bytes.AsSpan(0, checked((int)bounded))); }
    private static int ManagedFieldPosition(string text, ulong bytePosition) { var bytes = global::System.Text.Encoding.UTF8.GetBytes(text); var bounded = global::System.Math.Min(bytePosition, (ulong)bytes.Length); return global::System.Text.Encoding.UTF8.GetCharCount(bytes.AsSpan(0, checked((int)bounded))); }
    internal NativeFieldEdit ReplaceField(int start, int length, string replacement) { EnsureAlive(); string before; lock (stateGate) before = cachedText; start = global::System.Math.Clamp(start, 0, before.Length); length = global::System.Math.Clamp(length, 0, before.Length - start); var startBytes = global::System.Text.Encoding.UTF8.GetByteCount(before.AsSpan(0, start)); var lengthBytes = global::System.Text.Encoding.UTF8.GetByteCount(before.AsSpan(start, length)); var replacementBytes = global::System.Text.Encoding.UTF8.GetBytes(replacement ?? string.Empty); FieldEditResult result; fixed (byte* data = replacementBytes) Check(((delegate* unmanaged[Cdecl]<Handle, ulong, ulong, StringView, FieldEditResult*, int>)api.FieldReplace)(handle.Value, (ulong)startBytes, (ulong)lengthBytes, new StringView { Data = data, Size = (ulong)replacementBytes.Length }, &result)); var after = GetString(api.GetText); lock (stateGate) cachedText = after; return new NativeFieldEdit(after, ManagedFieldPosition(after, result.Anchor), ManagedFieldPosition(after, result.Caret), result.Changed != 0, result.CanUndo != 0, result.CanRedo != 0); }
    internal NativeFieldEdit FieldHistory(int direction) { EnsureAlive(); FieldEditResult result; Check(((delegate* unmanaged[Cdecl]<Handle, int, FieldEditResult*, int>)api.FieldHistory)(handle.Value, direction, &result)); var after = GetString(api.GetText); lock (stateGate) cachedText = after; return new NativeFieldEdit(after, ManagedFieldPosition(after, result.Anchor), ManagedFieldPosition(after, result.Caret), result.Changed != 0, result.CanUndo != 0, result.CanRedo != 0); }
    internal void ClearFieldHistory() { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, int>)api.FieldClearHistory)(handle.Value)); }
    internal uint CheckState
    {
        get { EnsureAlive(); uint value; Check(((delegate* unmanaged[Cdecl]<Handle, uint*, int>)api.GetCheckState)(handle.Value, &value)); return value; }
        set { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.SetCheckState)(handle.Value, value)); }
    }
    internal void SetRange(double minimum, double maximum) { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, double, double, int>)api.SetRange)(handle.Value, minimum, maximum)); }
    internal void GetRange(out double minimum, out double maximum) { EnsureAlive(); double minimumValue; double maximumValue; Check(((delegate* unmanaged[Cdecl]<Handle, double*, double*, int>)api.GetRange)(handle.Value, &minimumValue, &maximumValue)); minimum = minimumValue; maximum = maximumValue; }
    internal double RangeValue
    {
        get { EnsureAlive(); double value; Check(((delegate* unmanaged[Cdecl]<Handle, double*, int>)api.GetRangeValue)(handle.Value, &value)); return value; }
        set { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, double, int>)api.SetRangeValue)(handle.Value, value)); }
    }
    internal bool Capture
    {
        get { EnsureAlive(); uint value; Check(((delegate* unmanaged[Cdecl]<Handle, uint*, int>)api.GetPointerCapture)(handle.Value, &value)); return value != 0; }
        set { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.SetPointerCapture)(handle.Value, value ? 1u : 0u)); }
    }
    internal bool ShowPathDialog(uint kind, string title, string initialDirectory,
                                 string suggestedName, string defaultExtension,
                                 string filter, uint flags, out string selectedPath)
    {
        EnsureAlive();
        var titleBytes = Encoding.UTF8.GetBytes(title ?? string.Empty);
        var directoryBytes = Encoding.UTF8.GetBytes(initialDirectory ?? string.Empty);
        var suggestedBytes = Encoding.UTF8.GetBytes(suggestedName ?? string.Empty);
        var extensionBytes = Encoding.UTF8.GetBytes(defaultExtension ?? string.Empty);
        var filterBytes = Encoding.UTF8.GetBytes(filter ?? string.Empty);
        uint accepted;
        fixed (byte* titleData = titleBytes)
        fixed (byte* directoryData = directoryBytes)
        fixed (byte* suggestedData = suggestedBytes)
        fixed (byte* extensionData = extensionBytes)
        fixed (byte* filterData = filterBytes)
            Check(((delegate* unmanaged[Cdecl]<Handle, uint, StringView, StringView, StringView, StringView, StringView, uint, uint*, int>)api.ShowPathDialog)(
                handle.Value, kind,
                new StringView { Data = titleData, Size = (ulong)titleBytes.Length },
                new StringView { Data = directoryData, Size = (ulong)directoryBytes.Length },
                new StringView { Data = suggestedData, Size = (ulong)suggestedBytes.Length },
                new StringView { Data = extensionData, Size = (ulong)extensionBytes.Length },
                new StringView { Data = filterData, Size = (ulong)filterBytes.Length },
                flags, &accepted));
        selectedPath = accepted == 0 ? string.Empty : GetString(api.LastDialogPath);
        return accepted != 0;
    }
    internal void ShowToolTip(string text, int x, int y, int duration)
    {
        EnsureAlive();
        var bytes = Encoding.UTF8.GetBytes(text ?? string.Empty);
        fixed (byte* data = bytes)
            Check(((delegate* unmanaged[Cdecl]<Handle, StringView, double, double, uint, int>)api.ShowTooltip)(
                handle.Value, new StringView { Data = data, Size = (ulong)bytes.Length },
                x, y, checked((uint)duration)));
    }
    internal void HideToolTip() { EnsureAlive(); Check(((delegate* unmanaged[Cdecl]<Handle, int>)api.HideTooltip)(handle.Value)); }
    internal bool InvokeRequired => Environment.CurrentManagedThreadId != ownerThreadId;
    internal bool SupportsRaster { get { return supportsRaster && api.SetControlPng != 0; } }
    internal void SetRaster(byte[] encodedPng) { if (!SupportsRaster) return; fixed (byte* data = encodedPng) Check(((delegate* unmanaged[Cdecl]<Handle, byte*, ulong, int>)api.SetControlPng)(handle.Value, data, (ulong)encodedPng.Length)); }
    internal void SetRasterPixels(nint pixels, int width, int height, int rowBytes) { if (!SupportsRaster || api.SetControlPixels == 0 || exposesWindowSurface) return; if (pixels == 0 || width <= 0 || height <= 0 || rowBytes < checked(width * 4)) throw new global::System.ArgumentException("Invalid retained paint surface."); Check(((delegate* unmanaged[Cdecl]<Handle, byte*, uint, uint, ulong, uint, int>)api.SetControlPixels)(handle.Value, (byte*)pixels, checked((uint)width), checked((uint)height), checked((ulong)rowBytes), 1u)); }
    internal string RunWindow(bool autoClose, bool forceHeadless, bool autoActivate, bool popup)
    {
        var flags = (autoClose ? 1u : 0u) | (forceHeadless ? 2u : 0u) | (autoActivate ? 4u : 0u) | (popup ? 8u : 0u);
        if (traceControls) Console.Error.WriteLine($"facade-window=run|id={stableId}|type={managedTypeName}|popup={popup}|flags={flags}");
        Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)api.RunWindow)(handle.Value, flags));
        return GetString(api.LastHostTrace);
    }
    internal static void DoEvents(int ownerThreadId)
    {
        // GUI.Forms defines BeginInvoke as a strictly posted boundary: a native
        // input/lifecycle callback must unwind before work it posts can run.
        // This prevents DoEvents from turning pointer or load callbacks into an
        // accidental recursive dispatcher.
        if (nativeCallbackDepth != 0) return;
        try
        {
            if (!pendingByThread.TryGetValue(ownerThreadId, out var pending)) return;
            foreach (var item in pending.OrderBy(item => item.Key).ToArray())
            {
                try { item.Value.Execute(); }
                catch (Exception error) { Application.__ReportCallbackException(error); }
            }
            if (pending.IsEmpty) pendingByThread.TryRemove(ownerThreadId, out _);
        }
        finally { FlushWindowSurfaces(ownerThreadId); }
    }
    private static void Track(NativeAsyncResult pending) =>
        pendingByThread.GetOrAdd(pending.OwnerThreadId, _ => new()).TryAdd(pending.Id, pending);
    private static void Untrack(NativeAsyncResult pending)
    {
        if (!pendingByThread.TryGetValue(pending.OwnerThreadId, out var values)) return;
        values.TryRemove(pending.Id, out _);
        if (values.IsEmpty) pendingByThread.TryRemove(pending.OwnerThreadId, out _);
    }
    internal global::System.IAsyncResult BeginInvoke(global::System.Delegate method)
    {
        if (method is null) throw new global::System.ArgumentNullException(nameof(method));
        var declaringType = method.Method.DeclaringType?.FullName ?? string.Empty;
        if (traceDelegates && (declaringType.StartsWith("retired compatibility specimen.FrontEnds.SpyServer", StringComparison.Ordinal) ||
            declaringType == "retired compatibility specimen.MainForm" && method.Method.Name == "HandleFrontendControllerSampleRateChange"))
        {
            var strings = method.Target?.GetType().GetFields(global::System.Reflection.BindingFlags.Instance | global::System.Reflection.BindingFlags.Public | global::System.Reflection.BindingFlags.NonPublic)
                .Where(field => field.FieldType == typeof(string)).Select(field => field.GetValue(method.Target) as string).Where(value => value is not null) ?? [];
            Console.Error.WriteLine($"facade-delegate=begin-invoke|id={stableId}|method={declaringType}.{method.Method.Name}|thread={Environment.CurrentManagedThreadId}|strings={string.Join(';', strings)}");
        }
        var pending = new NativeAsyncResult(method, ownerThreadId);
        Track(pending);
        var root = GCHandle.Alloc(pending);
        var result = ((delegate* unmanaged[Cdecl]<Handle, delegate* unmanaged[Cdecl]<void*, uint, uint>, void*, int>)api.BeginInvoke)(
            handle.Value, (delegate* unmanaged[Cdecl]<void*, uint, uint>)(void*)dispatchThunkPointer, (void*)GCHandle.ToIntPtr(root));
        if (result != 0) { root.Free(); pending.Cancel(); Check(result); }
        return pending;
    }
    internal object? Invoke(global::System.Delegate method)
    {
        if (method is null) throw new global::System.ArgumentNullException(nameof(method));
        if (!InvokeRequired) return method.DynamicInvoke();
        var pending = (NativeAsyncResult)BeginInvoke(method);
        pending.AsyncWaitHandle.WaitOne();
        return pending.GetResult();
    }
    internal void RequestClose() { Check(((delegate* unmanaged[Cdecl]<Handle, int>)api.RequestClose)(handle.Value)); }
    public void Dispose() { if (IsDisposed) return; ReleaseSubscriptions(true); ReleaseWindowHandle(); handle.DisposeNative(); IsDisposed = true; GC.SuppressFinalize(this); }
    ~NativeControlBridge() { ReleaseSubscriptions(false); ReleaseWindowHandle(); }

    private void ReleaseWindowHandle()
    {
        nint value;
        ulong endpoint;
        windowSurfaceTimer?.Dispose();
        windowSurfaceTimer = null;
        global::System.Threading.Interlocked.Increment(ref windowSurfaceEpoch);
        global::System.Threading.Interlocked.Exchange(ref windowSurfaceDrainQueued, 0);
        global::System.Threading.Interlocked.Exchange(ref windowSurfaceDirtyAfterDrain, 0);
        global::System.Threading.Interlocked.Exchange(ref windowSurfaceProbePending, 0);
        windowSurfaces.TryRemove(surfaceId, out _);
        lock (windowHandleGate)
        {
            ReleaseCaptureSurface();
            value = windowHandle;
            endpoint = nativePaintEndpoint;
            windowHandle = 0;
            nativePaintEndpoint = 0;
            windowParent = 0;
        }
        if (endpoint != 0)
        {
            if (traceDirectGdiOwnership)
                global::System.Console.Error.WriteLine("gui-forms-direct-gdi=release|surface:" + surfaceId +
                    "|control:" + stableId + "|type:" + managedTypeName + "|endpoint:" + endpoint +
                    "|virtual-handle:0x" + ((nuint)value).ToString("x") +
                    "|thread:" + global::System.Environment.CurrentManagedThreadId);
            _ = ReleaseWindowsPaintEndpoint(endpoint);
            return;
        }
        if (value != 0) windowSurfacesByHandle.TryRemove(value, out _);
        if (value == 0 || !global::System.OperatingSystem.IsWindows()) return;
        if (global::System.Environment.CurrentManagedThreadId == ownerThreadId) _ = DestroyWindow(value);
        else _ = PostMessageW(value, 0x0010u, 0, 0);
    }

    private void ResizeWindowSurface(int width, int height)
    {
        if (!global::System.OperatingSystem.IsWindows()) return;
        width = global::System.Math.Max(1, width);
        height = global::System.Math.Max(1, height);
        ulong endpoint;
        lock (windowHandleGate) endpoint = nativePaintEndpoint;
        if (endpoint != 0)
        {
            if (traceDirectGdiOwnership)
                global::System.Console.Error.WriteLine("gui-forms-direct-gdi=resize|surface:" + surfaceId +
                    "|control:" + stableId + "|type:" + managedTypeName + "|endpoint:" + endpoint +
                    "|size:" + width + "x" + height + "|thread:" +
                    global::System.Environment.CurrentManagedThreadId);
            Check(ConfigureWindowsPaintEndpoint(endpoint,
                checked((uint)width), checked((uint)height)));
            lock (windowHandleGate) windowSize = new(width, height);
            return;
        }
        var changed = false;
        lock (windowHandleGate)
        {
            if (windowHandle == 0) return;
            changed = windowSize.Width != width || windowSize.Height != height;
            windowSize = new(width, height);
            if (changed)
            {
                global::System.Threading.Interlocked.Increment(ref windowSurfaceEpoch);
                windowSurfaceContentHash = 0;
                if (windowParent == 0) _ = SetWindowPos(windowHandle, 0, 0, 0, width, height, 0x0016u);
            }
        }
        if (changed) MarkWindowSurfaceDirty();
    }

    internal void ConfigureWindowSurface(global::System.Drawing.Point position,
                                         global::System.Drawing.Size size)
    {
        if (!global::System.OperatingSystem.IsWindows()) return;
        var normalized = new global::System.Drawing.Size(
            global::System.Math.Max(1, size.Width),
            global::System.Math.Max(1, size.Height));
        ulong endpoint;
        lock (windowHandleGate) endpoint = nativePaintEndpoint;
        if (endpoint != 0)
        {
            if (traceDirectGdiOwnership)
                global::System.Console.Error.WriteLine("gui-forms-direct-gdi=configure|surface:" + surfaceId +
                    "|control:" + stableId + "|type:" + managedTypeName + "|endpoint:" + endpoint +
                    "|position:" + position.X + "," + position.Y + "|size:" + normalized.Width + "x" +
                    normalized.Height + "|thread:" + global::System.Environment.CurrentManagedThreadId);
            Check(ConfigureWindowsPaintEndpoint(endpoint,
                checked((uint)normalized.Width), checked((uint)normalized.Height)));
            lock (windowHandleGate)
            {
                windowPosition = position;
                windowSize = normalized;
                windowSurfaceConfigured = true;
            }
            return;
        }
        var changed = false;
        lock (windowHandleGate)
        {
            changed = !windowSurfaceConfigured || windowPosition != position ||
                windowSize != normalized;
            windowPosition = position;
            if (windowSize != normalized)
            {
                windowSize = normalized;
                global::System.Threading.Interlocked.Increment(ref windowSurfaceEpoch);
                windowSurfaceContentHash = 0;
            }
            windowSurfaceConfigured = true;
        }
        if (changed) MarkWindowSurfaceDirty();
    }

    private static bool RegisterWindowSurfaceClass()
    {
        if (!global::System.OperatingSystem.IsWindows()) return false;
        var instance = GetModuleHandleW(null);
        if (instance == 0) return false;
        var value = new WindowClass
        {
            Style = 0x0020u,
            WindowProcedure = windowSurfaceThunkPointer,
            Instance = instance,
            Cursor = LoadCursorW(0, (nint)32512), // IDC_ARROW
            ClassName = windowSurfaceClassName,
        };
        var atom = RegisterClassW(ref value);
        return atom != 0 || global::System.Runtime.InteropServices.Marshal.GetLastWin32Error() == 1410;
    }

    internal static void FlushWindowSurfaces(int ownerThread)
    {
        if (!global::System.OperatingSystem.IsWindows() || flushingWindowSurfaces) return;
        flushingWindowSurfaces = true;
        try
        {
            foreach (var item in windowSurfaces.ToArray())
            {
                if (!item.Value.TryGetTarget(out var bridge) || bridge.IsDisposed)
                {
                    windowSurfaces.TryRemove(item.Key, out _);
                    continue;
                }
                if (bridge.ownerThreadId != ownerThread || !bridge.supportsRaster ||
                    !bridge.exposesWindowSurface) continue;
                try { bridge.DrainWindowSurfaceIfQueued(); }
                catch (global::System.Exception error) { Application.__ReportCallbackException(error); }
            }
        }
        finally { flushingWindowSurfaces = false; }
    }

    internal void TouchWindowSurface()
    {
        if (IsDisposed || !exposesWindowSurface || windowHandle == 0) return;
        MarkWindowSurfaceDirty();
    }

    private void MarkWindowSurfaceDirty()
    {
        ulong endpoint;
        lock (windowHandleGate) endpoint = nativePaintEndpoint;
        if (endpoint != 0)
        {
            Check(TouchWindowsPaintEndpoint(endpoint, 0));
            return;
        }
        global::System.Threading.Interlocked.Increment(ref windowSurfaceContentRevision);
        if (global::System.Threading.Volatile.Read(ref windowSurfaceDrainActive) != 0)
            global::System.Threading.Interlocked.Exchange(ref windowSurfaceDirtyAfterDrain, 1);
        if (!hasExplicitPresentBoundary)
            windowSurfaceTimer?.Change(WindowSurfaceMinimumProbeMilliseconds,
                global::System.Threading.Timeout.Infinite);
        QueueWindowSurfaceDrain();
    }

    private void QueueWindowSurfaceProbe()
    {
        if (IsDisposed || hasExplicitPresentBoundary || !exposesWindowSurface || windowHandle == 0) return;
        global::System.Threading.Interlocked.Exchange(ref windowSurfaceProbePending, 1);
        QueueWindowSurfaceDrain();
    }

    private void QueueWindowSurfaceDrain()
    {
        if (IsDisposed || !exposesWindowSurface || windowHandle == 0) return;
        if (global::System.Threading.Interlocked.Exchange(ref windowSurfaceDrainQueued, 1) != 0)
        {
            global::System.Threading.Interlocked.Increment(ref windowSurfaceDrainsCoalesced);
            return;
        }
        global::System.Threading.Interlocked.Increment(ref windowSurfaceDrainsQueued);
        if (!Application.__Post(ownerThreadId, DrainWindowSurfaceIfQueued))
            global::System.Threading.Interlocked.Exchange(ref windowSurfaceDrainQueued, 0);
    }

    private void DrainWindowSurfaceIfQueued()
    {
        if (global::System.Threading.Interlocked.Exchange(ref windowSurfaceDrainQueued, 0) == 0) return;
        DrainWindowSurfaceCore();
    }

    internal void DrainWindowSurfaceNow()
    {
        if (IsDisposed || !exposesWindowSurface || windowHandle == 0) return;
        ulong endpoint;
        lock (windowHandleGate) endpoint = nativePaintEndpoint;
        if (endpoint != 0)
        {
            Check(DrainWindowsPaintEndpoint(endpoint));
            return;
        }
        global::System.Threading.Interlocked.Exchange(ref windowSurfaceDrainQueued, 0);
        DrainWindowSurfaceCore();
    }

    private void DrainWindowSurfaceCore()
    {
        if (global::System.Threading.Interlocked.Exchange(ref windowSurfaceDrainActive, 1) != 0)
        {
            global::System.Threading.Interlocked.Exchange(ref windowSurfaceDirtyAfterDrain, 1);
            return;
        }
        var leaseRevision = global::System.Threading.Interlocked.Read(ref windowSurfaceContentRevision);
        var leaseEpoch = global::System.Threading.Interlocked.Read(ref windowSurfaceEpoch);
        var probe = global::System.Threading.Interlocked.Exchange(ref windowSurfaceProbePending, 0) != 0;
        var completed = false;
        var probeChanged = false;
        try
        {
            if (leaseRevision <= global::System.Threading.Interlocked.Read(ref windowSurfaceCapturedRevision) && !probe)
                return;
            global::System.Threading.Interlocked.Increment(ref windowSurfaceDrainsStarted);
            var processedRevision = SynchronizeWindowSurface(leaseRevision, leaseEpoch, out probeChanged);
            if (processedRevision < 0) return;
            completed = true;
            global::System.Threading.Interlocked.Exchange(ref windowSurfaceCapturedRevision, processedRevision);
            if (probeChanged) global::System.Threading.Interlocked.Increment(ref windowSurfaceDrainsCommitted);
            else global::System.Threading.Interlocked.Increment(ref windowSurfaceDrainsUnchanged);
            if (global::System.Environment.GetEnvironmentVariable("GUI_FORMS_TRACE_NATIVE_SURFACES") == "1")
                global::System.Console.Error.WriteLine("gui-forms-native-surface-drain=id:" + stableId + "|" + WindowSurfaceSnapshot());
        }
        finally
        {
            global::System.Threading.Interlocked.Exchange(ref windowSurfaceDrainActive, 0);
            if (probe && !hasExplicitPresentBoundary) RearmWindowSurfaceProbe(completed && probeChanged);
            var deferred = global::System.Threading.Interlocked.Exchange(ref windowSurfaceDirtyAfterDrain, 0) != 0;
            if (completed && (deferred ||
                global::System.Threading.Interlocked.Read(ref windowSurfaceContentRevision) >
                global::System.Threading.Interlocked.Read(ref windowSurfaceCapturedRevision)))
                QueueWindowSurfaceDrain();
        }
    }

    private void RearmWindowSurfaceProbe(bool changed)
    {
        windowSurfaceProbeIntervalMilliseconds = changed
            ? WindowSurfaceMinimumProbeMilliseconds
            : global::System.Math.Min(WindowSurfaceMaximumProbeMilliseconds,
                windowSurfaceProbeIntervalMilliseconds * 2);
        windowSurfaceTimer?.Change(windowSurfaceProbeIntervalMilliseconds,
            global::System.Threading.Timeout.Infinite);
    }

    internal string WindowSurfaceSnapshot()
    {
        ulong endpoint;
        lock (windowHandleGate) endpoint = nativePaintEndpoint;
        if (endpoint != 0)
        {
            ulong required = 0;
            var result = SnapshotWindowsPaintEndpoint(endpoint, null, 0, &required);
            if (result != 6 && result != 0) Check(result);
            if (required == 0) return string.Empty;
            var bytes = new byte[checked((int)required)];
            fixed (byte* data = bytes)
                Check(SnapshotWindowsPaintEndpoint(endpoint, data, required, &required));
            return Encoding.UTF8.GetString(bytes);
        }
        var content = global::System.Threading.Interlocked.Read(ref windowSurfaceContentRevision);
        var captured = global::System.Threading.Interlocked.Read(ref windowSurfaceCapturedRevision);
        var active = global::System.Threading.Volatile.Read(ref windowSurfaceDrainActive) != 0;
        var deferred = global::System.Threading.Volatile.Read(ref windowSurfaceDirtyAfterDrain) != 0;
        var queued = global::System.Threading.Volatile.Read(ref windowSurfaceDrainQueued) != 0;
        var state = IsDisposed || windowHandle == 0 ? "retired" :
            active && deferred ? "rendering_dirty" : active ? "rendering" :
            queued || content > captured ? "dirty_queued" : "clean";
        return "state:" + state + "|content:" + content + "|captured:" + captured +
            "|epoch:" + global::System.Threading.Interlocked.Read(ref windowSurfaceEpoch) +
            "|queued:" + (queued ? 1 : 0) +
            "|active:" + (active ? 1 : 0) +
            "|deferred:" + (deferred ? 1 : 0) +
            "|drains-queued:" + global::System.Threading.Interlocked.Read(ref windowSurfaceDrainsQueued) +
            "|drains-coalesced:" + global::System.Threading.Interlocked.Read(ref windowSurfaceDrainsCoalesced) +
            "|drains-started:" + global::System.Threading.Interlocked.Read(ref windowSurfaceDrainsStarted) +
            "|drains-committed:" + global::System.Threading.Interlocked.Read(ref windowSurfaceDrainsCommitted) +
            "|drains-unchanged:" + global::System.Threading.Interlocked.Read(ref windowSurfaceDrainsUnchanged) +
            "|content-hash:" + windowSurfaceContentHash.ToString("x16", global::System.Globalization.CultureInfo.InvariantCulture) +
            "|explicit:" + (hasExplicitPresentBoundary ? 1 : 0);
    }

    private long SynchronizeWindowSurface(long leaseRevision, long leaseEpoch,
                                          out bool changed)
    {
        changed = false;
        lock (windowHandleGate)
        {
            if (windowHandle == 0 || IsDisposed || !SupportsRaster || !exposesWindowSurface ||
                !windowSurfaceConfigured || leaseEpoch != windowSurfaceEpoch) return -1;
            Rect absolute;
            Check(((delegate* unmanaged[Cdecl]<Handle, Rect*, int>)api.GetControlAbsoluteBounds)(handle.Value, &absolute));
            windowPosition = new(global::System.Convert.ToInt32(global::System.Math.Round(absolute.X)),
                                 global::System.Convert.ToInt32(global::System.Math.Round(absolute.Y)));
            windowSize = new(global::System.Math.Max(1, global::System.Convert.ToInt32(global::System.Math.Round(absolute.Width))),
                             global::System.Math.Max(1, global::System.Convert.ToInt32(global::System.Math.Round(absolute.Height))));
            if (!haveAppliedWindowState || appliedWindowSize != windowSize)
            {
                // Wine discards GDI backing for a never-shown HWND. Keep the
                // lease compositor-backed but parked far outside the desktop,
                // disabled and non-activating. It is never parented into the
                // visible retained host, so it cannot flash over sibling controls.
                var flags = cachedVisible ? 0x0054u : 0x0094u;
                _ = SetWindowPos(windowHandle, 0, -32000, -32000,
                    windowSize.Width, windowSize.Height, flags);
                appliedWindowPosition = windowPosition;
                appliedWindowSize = windowSize;
                appliedWindowVisible = cachedVisible;
                haveAppliedWindowState = true;
                if (global::System.Environment.GetEnvironmentVariable("GUI_FORMS_TRACE_NATIVE_SURFACES") == "1" && windowSurfaceTraceCount++ < 4)
                    global::System.Console.Error.WriteLine("gui-forms-native-surface=id:" + stableId + "|type:" + managedTypeName + "|mode:offscreen-gdi-capture|size:" + windowSize.Width + "x" + windowSize.Height);
            }
            return CaptureWindowSurfacePixels(leaseRevision, leaseEpoch, out changed);
        }
    }

    private long CaptureWindowSurfacePixels(long leaseRevision, long leaseEpoch,
                                            out bool changed)
    {
        changed = false;
        if (!cachedVisible || windowSize.Width < 1 || windowSize.Height < 1 ||
            api.SetControlPixels == 0 || leaseEpoch != windowSurfaceEpoch) return -1;
        var source = GetDC(windowHandle);
        if (source == 0) return -1;
        try
        {
            EnsureCaptureSurface(source, windowSize.Width, windowSize.Height);
            if (captureDevice == 0 || capturePixels == null ||
                !BitBlt(captureDevice, 0, 0, captureWidth, captureHeight, source, 0, 0, 0x00cc0020u)) return -1;

            // GDI color copies commonly leave the alpha byte at zero. The retained
            // compositor consumes premultiplied BGRA, so an unset alpha would turn
            // a valid spectrum into transparent/black tiles.
            ulong hash = 1469598103934665603UL;
            var count = checked(captureWidth * captureHeight);
            var pixel = (uint*)capturePixels;
            for (var index = 0; index < count; ++index)
            {
                var opaque = pixel[index] | 0xff000000u;
                pixel[index] = opaque;
                hash = (hash ^ opaque) * 1099511628211UL;
            }
            if (hash == windowSurfaceContentHash) return leaseRevision;
            if (leaseEpoch != windowSurfaceEpoch) return -1;
            if (leaseRevision <= global::System.Threading.Interlocked.Read(ref windowSurfaceCapturedRevision))
                leaseRevision = global::System.Threading.Interlocked.Increment(ref windowSurfaceContentRevision);
            Check(((delegate* unmanaged[Cdecl]<Handle, byte*, uint, uint, ulong, uint, int>)api.SetControlPixels)(
                handle.Value, capturePixels, checked((uint)captureWidth), checked((uint)captureHeight),
                checked((ulong)captureWidth * 4UL), 1u));
            windowSurfaceContentHash = hash;
            changed = true;
            if (global::System.Environment.GetEnvironmentVariable("GUI_FORMS_TRACE_NATIVE_SURFACE_CONTENT") == "1" &&
                windowSurfaceContentTraceCount++ < 16)
                global::System.Console.Error.WriteLine("gui-forms-native-surface-content=id:" + stableId +
                    "|type:" + managedTypeName + "|hash:" + hash.ToString("x16") +
                    "|size:" + captureWidth + "x" + captureHeight);
            return leaseRevision;
        }
        finally { _ = ReleaseDC(windowHandle, source); }
    }

    private void EnsureCaptureSurface(nint source, int width, int height)
    {
        if (captureDevice != 0 && capturePixels != null && captureWidth == width && captureHeight == height) return;
        ReleaseCaptureSurface();
        var info = new BitmapInfo { Header = new BitmapInfoHeader { Size = 40, Width = width,
            Height = -height, Planes = 1, BitCount = 32, SizeImage = checked((uint)(width * height * 4)) } };
        captureDevice = CreateCompatibleDC(source);
        if (captureDevice == 0) return;
        void* bits;
        captureBitmap = CreateDIBSection(source, ref info, 0, &bits, 0, 0);
        if (captureBitmap == 0 || bits == null) { ReleaseCaptureSurface(); return; }
        capturePreviousBitmap = SelectObject(captureDevice, captureBitmap);
        capturePixels = (byte*)bits;
        captureWidth = width;
        captureHeight = height;
        windowSurfaceContentHash = 0;
    }

    private void ReleaseCaptureSurface()
    {
        if (captureDevice != 0 && capturePreviousBitmap != 0) _ = SelectObject(captureDevice, capturePreviousBitmap);
        if (captureBitmap != 0) _ = DeleteObject(captureBitmap);
        if (captureDevice != 0) _ = DeleteDC(captureDevice);
        captureDevice = captureBitmap = capturePreviousBitmap = 0;
        capturePixels = null;
        captureWidth = captureHeight = 0;
    }

    private static nint WindowSurfaceProcedure(nint window, uint message, nint wParam, nint lParam)
    {
        // Direct-GDI consumers own the pixels. Validating WM_PAINT without
        // erasing preserves the last BitBlt across unrelated retained-host
        // repaints; input continues through the retained host.
        if (message == 0x0014u) return 1; // WM_ERASEBKGND
        if (message == 0x000fu)
        {
            PaintStruct paint;
            _ = BeginPaint(window, &paint);
            _ = EndPaint(window, &paint);
            if (windowSurfacesByHandle.TryGetValue(window, out var weak) &&
                weak.TryGetTarget(out var bridge) && !bridge.IsDisposed)
                bridge.MarkWindowSurfaceDirty();
            return 0;
        }
        if (message == 0x83f1u)
        {
            if (windowSurfacesByHandle.TryGetValue(window, out var weak) && weak.TryGetTarget(out var bridge) &&
                !bridge.IsDisposed)
            {
                bridge.hasExplicitPresentBoundary = true;
                bridge.windowSurfaceTimer?.Change(global::System.Threading.Timeout.Infinite,
                    global::System.Threading.Timeout.Infinite);
                bridge.MarkWindowSurfaceDirty();
            }
            return 0;
        }
        if (message == 0x0084u) return new nint(-1); // HTTRANSPARENT
        return DefWindowProcW(window, message, wParam, lParam);
    }

    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern nint CreateWindowExW(uint exStyle, string className, string windowName,
        uint style, int x, int y, int width, int height, nint parent, nint menu,
        nint instance, nint parameter);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DestroyWindow(nint window);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool PostMessageW(nint window, uint message, nint wParam, nint lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern ushort RegisterClassW(ref WindowClass value);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint FindWindowExW(nint parent, nint childAfter, string className, string? windowName);
    [DllImport("user32.dll")]
    private static extern uint GetWindowThreadProcessId(nint window, out uint processId);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern nint SetParent(nint child, nint parent);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern nint SetWindowLongPtrW(nint window, int index, nint value);
    [DllImport("user32.dll", SetLastError = true)]
    private static extern nint GetWindowLongPtrW(nint window, int index);
    [DllImport("user32.dll")]
    private static extern nint DefWindowProcW(nint window, uint message, nint wParam, nint lParam);
    [DllImport("user32.dll")]
    private static extern nint BeginPaint(nint window, PaintStruct* paint);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EndPaint(nint window, PaintStruct* paint);
    [DllImport("user32.dll")]
    private static extern nint GetDC(nint window);
    [DllImport("user32.dll")]
    private static extern int ReleaseDC(nint window, nint device);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern nint LoadCursorW(nint instance, nint cursorName);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnableWindow(nint window, bool enable);
    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowPos(nint window, nint insertAfter, int x, int y,
        int width, int height, uint flags);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern nint GetModuleHandleW(string? moduleName);
    [DllImport("kernel32.dll")]
    private static extern uint GetCurrentProcessId();
    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
    private static extern nint GetProcAddress(nint module, string name);
    [DllImport("gdi32.dll")]
    private static extern nint CreateCompatibleDC(nint device);
    [DllImport("gdi32.dll")]
    private static extern nint CreateDIBSection(nint device, ref BitmapInfo info, uint usage,
        void** bits, nint section, uint offset);
    [DllImport("gdi32.dll")]
    private static extern nint SelectObject(nint device, nint value);
    [DllImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeleteObject(nint value);
    [DllImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool DeleteDC(nint device);
    [DllImport("gdi32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool BitBlt(nint destination, int x, int y, int width, int height,
        nint source, int sourceX, int sourceY, uint operation);

    private void SetString(nint operation, string value, NativeChange change)
    {
        if (traceControls) Console.Error.WriteLine($"facade-control=set|id={stableId}|type={managedTypeName}|change={change}|value={value.Replace('\r', ' ').Replace('\n', ' ')}");
        var bytes = Encoding.UTF8.GetBytes(value);
        lock (stateGate) { if (change == NativeChange.Name) cachedName = value; else cachedText = value; }
        fixed (byte* data = bytes)
            Check(((delegate* unmanaged[Cdecl]<Handle, StringView, int>)operation)(handle.Value, new StringView { Data = data, Size = (ulong)bytes.Length }));
    }

    private string GetString(nint operation)
    {
        ulong required = 0;
        var result = ((delegate* unmanaged[Cdecl]<Handle, byte*, ulong, ulong*, int>)operation)(handle.Value, null, 0, &required);
        if (result != 6 && result != 0) Check(result);
        if (required == 0) return string.Empty;
        var bytes = new byte[checked((int)required)];
        fixed (byte* data = bytes) Check(((delegate* unmanaged[Cdecl]<Handle, byte*, ulong, ulong*, int>)operation)(handle.Value, data, required, &required));
        return Encoding.UTF8.GetString(bytes);
    }

    private bool GetBool(nint operation) { uint value; Check(((delegate* unmanaged[Cdecl]<Handle, uint*, int>)operation)(handle.Value, &value)); return value != 0; }
    private void SetBool(nint operation, bool value, NativeChange change) { if (traceControls) Console.Error.WriteLine($"facade-control=set|id={stableId}|type={managedTypeName}|change={change}|value={value}"); if (change == NativeChange.Visible) cachedVisible = value; else cachedEnabled = value; Check(((delegate* unmanaged[Cdecl]<Handle, uint, int>)operation)(handle.Value, value ? 1u : 0u)); }

    private void SubscribeTyped(NativeEvent kind)
    {
        Handle token;
        Check(((delegate* unmanaged[Cdecl]<Handle, uint, delegate* unmanaged[Cdecl]<Handle, uint, void*, uint>, void*, Handle*, int>)api.SubscribeV2)(
            handle.Value, (uint)kind, &EventCallback, (void*)GCHandle.ToIntPtr(callbackRoot), &token));
        subscriptions.Add(token);
    }

    private void SynchronizeManagedBoundsFromNative()
    {
        Rect absolute;
        Check(((delegate* unmanaged[Cdecl]<Handle, Rect*, int>)api.GetControlAbsoluteBounds)(handle.Value, &absolute));
        var nextSize = new global::System.Drawing.Size(
            global::System.Math.Max(0, global::System.Convert.ToInt32(global::System.Math.Round(absolute.Width))),
            global::System.Math.Max(0, global::System.Convert.ToInt32(global::System.Math.Round(absolute.Height))));
        var changed = false;
        lock (stateGate)
        {
            if (cachedBounds.Size != nextSize)
            {
                cachedBounds = new global::System.Drawing.Rectangle(cachedBounds.Location, nextSize);
                changed = true;
            }
        }
        if (changed) Changed?.Invoke(NativeChange.Bounds);
    }

    private void QueueManagedBoundsSynchronization()
    {
        if (IsDisposed || global::System.Threading.Interlocked.Exchange(
                ref boundsSynchronizationPending, 1) != 0) return;
        if (!Application.__Post(ownerThreadId, () =>
        {
            global::System.Threading.Interlocked.Exchange(ref boundsSynchronizationPending, 0);
            if (IsDisposed) return;
            try { SynchronizeManagedBoundsFromNative(); }
            catch (global::System.Exception error) { Application.__ReportCallbackException(error); }
        })) global::System.Threading.Interlocked.Exchange(ref boundsSynchronizationPending, 0);
    }

    private void SubscribePointer()
    {
        Handle token;
        Check(((delegate* unmanaged[Cdecl]<Handle, delegate* unmanaged[Cdecl]<Handle, uint, double, double, double, uint, void*, uint>, void*, Handle*, int>)api.SubscribePointer)(
            handle.Value, &PointerCallback, (void*)GCHandle.ToIntPtr(callbackRoot), &token));
        subscriptions.Add(token);
    }

    private void SubscribeKey()
    {
        Handle token;
        Check(((delegate* unmanaged[Cdecl]<Handle, delegate* unmanaged[Cdecl]<Handle, uint, uint, uint, uint, void*, uint>, void*, Handle*, int>)api.SubscribeKey)(
            handle.Value, &KeyCallback, (void*)GCHandle.ToIntPtr(callbackRoot), &token));
        subscriptions.Add(token);
    }

    private void SubscribeKeyPreview()
    {
        Handle token;
        Check(((delegate* unmanaged[Cdecl]<Handle, delegate* unmanaged[Cdecl]<Handle, uint, uint, uint, uint, void*, uint>, void*, Handle*, int>)api.SubscribeKeyPreview)(
            handle.Value, &KeyPreviewCallback, (void*)GCHandle.ToIntPtr(callbackRoot), &token));
        subscriptions.Add(token);
    }

    private void SubscribeText()
    {
        Handle token;
        Check(((delegate* unmanaged[Cdecl]<Handle, delegate* unmanaged[Cdecl]<Handle, StringView, uint, int, int, void*, uint>, void*, Handle*, int>)api.SubscribeText)(
            handle.Value, &TextCallback, (void*)GCHandle.ToIntPtr(callbackRoot), &token));
        subscriptions.Add(token);
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static uint EventCallback(Handle sender, uint kind, void* context)
    {
        if (context == null || GCHandle.FromIntPtr((nint)context).Target is not NativeControlBridge bridge) return 0;
        ++nativeCallbackDepth;
        try { if ((NativeEvent)kind == NativeEvent.BoundsChanged) { bridge.QueueManagedBoundsSynchronization(); return 0; } return bridge.NativeEventRaised?.Invoke((NativeEvent)kind) == true ? 1u : 0u; }
        catch (Exception error) { Application.__ReportCallbackException(error); return 2u; }
        finally { bridge.TouchWindowSurface(); ExitNativeCallback(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static uint PointerCallback(Handle sender, uint kind, double x, double y,
                                        double wheelDelta, uint button, void* context)
    {
        if (context == null || GCHandle.FromIntPtr((nint)context).Target is not NativeControlBridge bridge) return 0;
        ++nativeCallbackDepth;
        try { bridge.PointerRaised?.Invoke(new NativePointer(kind, x, y, wheelDelta, button)); return 0; }
        catch (Exception error) { Application.__ReportCallbackException(error); return 2u; }
        finally { bridge.TouchWindowSurface(); ExitNativeCallback(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static uint KeyCallback(Handle sender, uint kind, uint physicalKey,
                                    uint modifiers, uint repeat, void* context)
    {
        if (context == null || GCHandle.FromIntPtr((nint)context).Target is not NativeControlBridge bridge) return 0;
        ++nativeCallbackDepth;
        try { bridge.KeyRaised?.Invoke(new NativeKey(kind, physicalKey, modifiers, repeat != 0)); return 0; }
        catch (Exception error) { Application.__ReportCallbackException(error); return 2u; }
        finally { bridge.TouchWindowSurface(); ExitNativeCallback(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static uint KeyPreviewCallback(Handle sender, uint kind, uint physicalKey,
                                           uint modifiers, uint repeat, void* context)
    {
        if (context == null || GCHandle.FromIntPtr((nint)context).Target is not NativeControlBridge bridge) return 0;
        ++nativeCallbackDepth;
        try { return bridge.KeyPreviewRaised?.Invoke(new NativeKey(kind, physicalKey, modifiers, repeat != 0)) == true ? 1u : 0u; }
        catch (Exception error) { Application.__ReportCallbackException(error); return 2u; }
        finally { bridge.TouchWindowSurface(); ExitNativeCallback(); }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static uint TextCallback(Handle sender, StringView text, uint composing,
                                     int replacementStart, int replacementLength,
                                     void* context)
    {
        if (context == null || GCHandle.FromIntPtr((nint)context).Target is not NativeControlBridge bridge) return 0;
        ++nativeCallbackDepth;
        try { var value = text.Data == null || text.Size == 0 ? string.Empty : Encoding.UTF8.GetString(text.Data, checked((int)text.Size)); bridge.TextRaised?.Invoke(value, composing != 0, replacementStart, replacementLength); return 0; }
        catch (Exception error) { Application.__ReportCallbackException(error); return 2u; }
        finally { bridge.TouchWindowSurface(); ExitNativeCallback(); }
    }

    private static uint DispatchCallback(void* context, uint cancelled)
    {
        if (context == null) return 0;
        var root = GCHandle.FromIntPtr((nint)context);
        ++nativeCallbackDepth;
        try
        {
            if (root.Target is NativeAsyncResult pending)
            {
                if (cancelled != 0) pending.Cancel();
                else pending.Execute();
            }
            return 0;
        }
        catch (Exception error)
        {
            Application.__ReportCallbackException(error);
            return 2u;
        }
        finally { ExitNativeCallback(); root.Free(); }
    }

    private void ReleaseSubscriptions(bool check)
    {
        var disconnected = true;
        foreach (var subscription in subscriptions)
        {
            var result = ((delegate* unmanaged[Cdecl]<Handle, int>)api.Disconnect)(subscription);
            if (check) Check(result);
            if (result != 0) disconnected = false;
        }
        if (disconnected)
        {
            subscriptions.Clear();
            if (callbackRoot.IsAllocated) callbackRoot.Free();
        }
    }

    internal sealed unsafe class PropertyObjectAdapter : IDisposable
    {
        private readonly object target;
        private readonly PropertyGrid ownerGrid;
        private readonly SafeControlHandle handle;
        private readonly global::System.Collections.Generic.List<PropertyState> properties = new();
        private readonly int ownerThreadId = global::System.Environment.CurrentManagedThreadId;
        private bool disposed;

        internal PropertyObjectAdapter(object target, PropertyGrid ownerGrid)
        {
            this.target = target ?? throw new global::System.ArgumentNullException(nameof(target));
            this.ownerGrid = ownerGrid ?? throw new global::System.ArgumentNullException(nameof(ownerGrid));
            var stableId = "forms.PropertyObject." + global::System.Threading.Interlocked.Increment(ref nextId).ToString(global::System.Globalization.CultureInfo.InvariantCulture);
            var stableBytes = global::System.Text.Encoding.UTF8.GetBytes(stableId);
            Handle value;
            fixed (byte* data = stableBytes)
                Check(((delegate* unmanaged[Cdecl]<uint, StringView, Handle*, int>)api.ControlCreateKind)(22u, new StringView { Data = data, Size = (ulong)stableBytes.Length }, &value));
            handle = new SafeControlHandle(value);
            try
            {
                var descriptors = global::System.ComponentModel.TypeDescriptor.GetProperties(target);
                var projectedCount = 0;
                foreach (global::System.ComponentModel.PropertyDescriptor descriptor in descriptors)
                {
                    if (!descriptor.IsBrowsable) continue;
                    if (++projectedCount > 256) throw new global::System.NotSupportedException("GUI.Forms projects at most 256 browsable properties per managed object.");
                    var property = new PropertyState(this, descriptor);
                    property.Define();
                    properties.Add(property);
                }
            }
            catch
            {
                foreach (var property in properties) property.Dispose();
                handle.DisposeNative();
                throw;
            }
        }

        internal Handle Value => handle.Value;

        private void Notify(PropertyState property)
        {
            if (disposed || property.SuppressNotifications != 0) return;
            if (global::System.Environment.CurrentManagedThreadId != ownerThreadId)
            {
                _ = Application.__Post(ownerThreadId, () => Notify(property));
                return;
            }
            var bytes = global::System.Text.Encoding.UTF8.GetBytes(property.Descriptor.Name);
            fixed (byte* data = bytes)
                Check(((delegate* unmanaged[Cdecl]<Handle, StringView, int>)api.PropertyObjectNotifyChanged)(handle.Value, new StringView { Data = data, Size = (ulong)bytes.Length }));
        }

        public void Dispose()
        {
            if (disposed) return;
            disposed = true;
            try { handle.DisposeNative(); }
            finally
            {
                foreach (var property in properties) property.Dispose();
                properties.Clear();
                global::System.GC.SuppressFinalize(this);
            }
        }

        private enum Projection : uint { Boolean = 1, Signed = 2, Unsigned = 3, Number = 4, Text = 5, Color = 10, Enumeration = 13 }

        private sealed unsafe class Utf8Lease : IDisposable
        {
            private readonly global::System.Collections.Generic.List<nint> allocations = new();
            internal StringView View(string? value)
            {
                value ??= string.Empty;
                if (value.Length == 0) return default;
                var bytes = global::System.Text.Encoding.UTF8.GetBytes(value);
                var pointer = global::System.Runtime.InteropServices.Marshal.AllocHGlobal(bytes.Length);
                allocations.Add(pointer);
                global::System.Runtime.InteropServices.Marshal.Copy(bytes, 0, pointer, bytes.Length);
                return new StringView { Data = (byte*)pointer, Size = (ulong)bytes.Length };
            }
            public void Dispose()
            {
                foreach (var pointer in allocations) global::System.Runtime.InteropServices.Marshal.FreeHGlobal(pointer);
                allocations.Clear();
            }
        }

        private sealed unsafe class PropertyState : IDisposable
        {
            private readonly PropertyObjectAdapter owner;
            private readonly global::System.Type payloadType;
            private readonly Projection projection;
            private readonly global::System.ComponentModel.TypeConverter converter;
            private readonly global::System.Drawing.Design.UITypeEditor? editor;
            private readonly global::System.Drawing.Design.UITypeEditorEditStyle editorStyle;
            private readonly bool nullable;
            private readonly bool resettable;
            private readonly global::System.EventHandler valueChanged;
            private GCHandle callbackRoot;
            private bool changeHooked;
            internal int SuppressNotifications;
            internal global::System.ComponentModel.PropertyDescriptor Descriptor { get; }

            internal PropertyState(PropertyObjectAdapter owner, global::System.ComponentModel.PropertyDescriptor descriptor)
            {
                this.owner = owner;
                Descriptor = descriptor;
                nullable = !descriptor.PropertyType.IsValueType || global::System.Nullable.GetUnderlyingType(descriptor.PropertyType) is not null;
                payloadType = global::System.Nullable.GetUnderlyingType(descriptor.PropertyType) ?? descriptor.PropertyType;
                converter = descriptor.Converter;
                projection = SelectProjection(payloadType, converter, descriptor.IsReadOnly);
                if (projection == Projection.Enumeration && global::System.Enum.GetUnderlyingType(payloadType) == typeof(ulong))
                    throw new global::System.NotSupportedException("GUI.Forms ABI 0.23 does not project UInt64-backed enums.");
                resettable = !descriptor.IsReadOnly && (descriptor.Attributes[typeof(global::System.ComponentModel.DefaultValueAttribute)] is not null || descriptor.CanResetValue(owner.target));
                if (!descriptor.IsReadOnly)
                {
                    editor = descriptor.GetEditor(typeof(global::System.Drawing.Design.UITypeEditor)) as global::System.Drawing.Design.UITypeEditor;
                    if (editor is not null) editorStyle = editor.GetEditStyle(new EditorContext(owner.target, descriptor));
                }
                valueChanged = (_, _) => owner.Notify(this);
            }

            private static Projection SelectProjection(global::System.Type type, global::System.ComponentModel.TypeConverter converter, bool readOnly)
            {
                if (type == typeof(bool)) return Projection.Boolean;
                if (type == typeof(sbyte) || type == typeof(short) || type == typeof(int) || type == typeof(long)) return Projection.Signed;
                if (type == typeof(byte) || type == typeof(ushort) || type == typeof(uint) || type == typeof(ulong)) return Projection.Unsigned;
                if (type == typeof(float) || type == typeof(double) || type == typeof(decimal)) return Projection.Number;
                if (type == typeof(string) || type == typeof(char)) return Projection.Text;
                if (type == typeof(global::System.Drawing.Color)) return Projection.Color;
                if (type.IsEnum) return Projection.Enumeration;
                if (converter.CanConvertTo(typeof(string)) && (readOnly || converter.CanConvertFrom(typeof(string)))) return Projection.Text;
                throw new global::System.NotSupportedException("GUI.Forms cannot project managed property type " + type.FullName + " through ABI 0.23.");
            }

            private string ConverterIdentity()
            {
                var identity = "managed:" + (Descriptor.ComponentType?.FullName ?? "object") + ":" + Descriptor.Name + ":" + (converter.GetType().FullName ?? "converter");
                if (identity.Length <= 240) return identity;
                return "managed:" + identity.GetHashCode(global::System.StringComparison.Ordinal).ToString("X8", global::System.Globalization.CultureInfo.InvariantCulture) + ":" + Descriptor.Name;
            }

            private string EditorIdentity()
            {
                if (editor is null || editorStyle == global::System.Drawing.Design.UITypeEditorEditStyle.None) return string.Empty;
                var identity = "managed-editor:" + (Descriptor.ComponentType?.FullName ?? "object") + ":" + Descriptor.Name + ":" + (editor.GetType().FullName ?? "editor") + ":" + editorStyle;
                if (identity.Length <= 240) return identity;
                return "managed-editor:" + identity.GetHashCode(global::System.StringComparison.Ordinal).ToString("X8", global::System.Globalization.CultureInfo.InvariantCulture) + ":" + Descriptor.Name;
            }

            internal void Define()
            {
                using var lease = new Utf8Lease();
                var enumChoices = BuildEnumChoices(lease);
                var standards = BuildStandards(lease);
                var flags = 1u | 4u;
                if (!Descriptor.IsReadOnly) flags |= 2u;
                if (nullable) flags |= 8u;
                if (converter.GetStandardValuesExclusive(null)) flags |= 16u;
                if (resettable) flags |= 32u;
                if (Descriptor.SupportsChangeEvents) flags |= 64u;
                if (payloadType.IsEnum && payloadType.IsDefined(typeof(global::System.FlagsAttribute), false)) flags |= 128u;
                callbackRoot = GCHandle.Alloc(this);
                try
                {
                    fixed (PropertyEnumChoice* choiceData = enumChoices)
                    fixed (PropertyValue* standardData = standards)
                    {
                        var descriptor = new PropertyDescriptorV1
                        {
                            StructSize = (uint)sizeof(PropertyDescriptorV1), Kind = (uint)projection, Flags = flags,
                            Name = lease.View(Descriptor.Name), Category = lease.View(Descriptor.Category), Description = lease.View(Descriptor.Description),
                            EnumTypeName = lease.View(payloadType.IsEnum ? payloadType.FullName : string.Empty),
                            EnumChoices = choiceData, EnumChoiceCount = (ulong)enumChoices.Length,
                            StandardValues = standardData, StandardValueCount = (ulong)standards.Length,
                            ConverterName = lease.View(ConverterIdentity()), EditorName = lease.View(EditorIdentity()),
                        };
                        var callbacks = new PropertyCallbacksV1
                        {
                            StructSize = (uint)sizeof(PropertyCallbacksV1), Context = (void*)GCHandle.ToIntPtr(callbackRoot),
                            Get = (nint)(delegate* unmanaged[Cdecl]<void*, PropertyValue*, byte*, ulong, ulong*, uint>)&GetCallback,
                            Set = Descriptor.IsReadOnly ? 0 : (nint)(delegate* unmanaged[Cdecl]<void*, PropertyValue*, uint>)&SetCallback,
                            Reset = resettable ? (nint)(delegate* unmanaged[Cdecl]<void*, uint>)&ResetCallback : 0,
                            ShouldSerialize = (nint)(delegate* unmanaged[Cdecl]<void*, uint*, uint>)&ShouldSerializeCallback,
                            Format = (nint)(delegate* unmanaged[Cdecl]<void*, PropertyValue*, byte*, ulong, ulong*, uint>)&FormatCallback,
                            Parse = Descriptor.IsReadOnly ? 0 : (nint)(delegate* unmanaged[Cdecl]<void*, StringView, PropertyValue*, byte*, ulong, ulong*, uint>)&ParseCallback,
                            Edit = editor is null || editorStyle == global::System.Drawing.Design.UITypeEditorEditStyle.None ? 0 : (nint)(delegate* unmanaged[Cdecl]<void*, PropertyValue*, PropertyValue*, byte*, ulong, ulong*, uint>)&EditCallback,
                        };
                        Check(((delegate* unmanaged[Cdecl]<Handle, PropertyDescriptorV1*, PropertyCallbacksV1*, int>)api.PropertyObjectDefine)(owner.handle.Value, &descriptor, &callbacks));
                    }
                    if (Descriptor.SupportsChangeEvents)
                    {
                        Descriptor.AddValueChanged(owner.target, valueChanged);
                        changeHooked = true;
                    }
                }
                catch
                {
                    if (callbackRoot.IsAllocated) callbackRoot.Free();
                    throw;
                }
            }

            private PropertyEnumChoice[] BuildEnumChoices(Utf8Lease lease)
            {
                if (!payloadType.IsEnum) return global::System.Array.Empty<PropertyEnumChoice>();
                var names = global::System.Enum.GetNames(payloadType);
                if (names.Length > 256) throw new global::System.NotSupportedException("GUI.Forms projects at most 256 enum choices.");
                var result = new PropertyEnumChoice[names.Length];
                for (var index = 0; index < names.Length; ++index)
                {
                    var value = global::System.Enum.Parse(payloadType, names[index]);
                    result[index] = new PropertyEnumChoice { Name = lease.View(names[index]), Value = global::System.Convert.ToInt64(value, global::System.Globalization.CultureInfo.InvariantCulture) };
                }
                return result;
            }

            private PropertyValue[] BuildStandards(Utf8Lease lease)
            {
                if (!converter.GetStandardValuesSupported(null)) return global::System.Array.Empty<PropertyValue>();
                var values = converter.GetStandardValues(null);
                if (values is null || values.Count == 0) return global::System.Array.Empty<PropertyValue>();
                if (values.Count > 256) throw new global::System.NotSupportedException("GUI.Forms projects at most 256 standard values.");
                var result = new global::System.Collections.Generic.List<PropertyValue>();
                var identities = new global::System.Collections.Generic.HashSet<string>(global::System.StringComparer.Ordinal);
                foreach (var item in values)
                {
                    var value = Encode(item, out var valueText);
                    if (valueText.Length != 0) value.TextValue = lease.View(valueText);
                    var identity = value.Kind + ":" + value.BooleanValue + ":" + value.SignedValue + ":" + value.UnsignedValue + ":" + value.NumberValue.ToString("R", global::System.Globalization.CultureInfo.InvariantCulture) + ":" + value.ColorArgb + ":" + valueText;
                    if (identities.Add(identity)) result.Add(value);
                }
                return result.ToArray();
            }

            private PropertyValue Encode(object? input, out string text)
            {
                text = string.Empty;
                if (input is null) return new PropertyValue { Kind = 0u };
                switch (projection)
                {
                    case Projection.Boolean: return new PropertyValue { Kind = (uint)projection, BooleanValue = (bool)global::System.Convert.ChangeType(input, typeof(bool), global::System.Globalization.CultureInfo.InvariantCulture) ? 1u : 0u };
                    case Projection.Signed: return new PropertyValue { Kind = (uint)projection, SignedValue = global::System.Convert.ToInt64(input, global::System.Globalization.CultureInfo.InvariantCulture) };
                    case Projection.Unsigned: return new PropertyValue { Kind = (uint)projection, UnsignedValue = global::System.Convert.ToUInt64(input, global::System.Globalization.CultureInfo.InvariantCulture) };
                    case Projection.Number: return new PropertyValue { Kind = (uint)projection, NumberValue = global::System.Convert.ToDouble(input, global::System.Globalization.CultureInfo.InvariantCulture) };
                    case Projection.Color: return new PropertyValue { Kind = (uint)projection, ColorArgb = unchecked((uint)((global::System.Drawing.Color)input).ToArgb()) };
                    case Projection.Enumeration: text = global::System.Enum.Format(payloadType, input, "G"); return new PropertyValue { Kind = (uint)projection, SignedValue = global::System.Convert.ToInt64(input, global::System.Globalization.CultureInfo.InvariantCulture) };
                    default: text = input is string stringValue ? stringValue : converter.ConvertToString(null, global::System.Globalization.CultureInfo.CurrentCulture, input) ?? string.Empty; return new PropertyValue { Kind = (uint)projection };
                }
            }

            private object? Decode(PropertyValue* value)
            {
                if (value->Kind == 0u)
                {
                    if (!nullable) throw new global::System.InvalidOperationException("A non-nullable property cannot receive null.");
                    return null;
                }
                if (value->Kind != (uint)projection) throw new global::System.InvalidOperationException("Native property kind does not match the managed descriptor.");
                switch (projection)
                {
                    case Projection.Boolean: return value->BooleanValue != 0u;
                    case Projection.Signed: return global::System.Convert.ChangeType(value->SignedValue, payloadType, global::System.Globalization.CultureInfo.InvariantCulture);
                    case Projection.Unsigned: return global::System.Convert.ChangeType(value->UnsignedValue, payloadType, global::System.Globalization.CultureInfo.InvariantCulture);
                    case Projection.Number: return global::System.Convert.ChangeType(value->NumberValue, payloadType, global::System.Globalization.CultureInfo.InvariantCulture);
                    case Projection.Color: return global::System.Drawing.Color.FromArgb(unchecked((int)value->ColorArgb));
                    case Projection.Enumeration: return global::System.Enum.ToObject(payloadType, value->SignedValue);
                    default:
                        var stringValue = Read(value->TextValue);
                        if (payloadType == typeof(string)) return stringValue;
                        if (payloadType == typeof(char)) return stringValue.Length == 1 ? stringValue[0] : throw new global::System.FormatException("Character properties require exactly one character.");
                        return converter.ConvertFromString(null, global::System.Globalization.CultureInfo.CurrentCulture, stringValue);
                }
            }

            private static string Read(StringView value) => value.Data == null || value.Size == 0 ? string.Empty : global::System.Text.Encoding.UTF8.GetString(value.Data, checked((int)value.Size));
            private static uint Write(string text, byte* output, ulong capacity, ulong* required)
            {
                var bytes = global::System.Text.Encoding.UTF8.GetBytes(text ?? string.Empty);
                *required = (ulong)bytes.Length;
                if (capacity < (ulong)bytes.Length || (bytes.Length != 0 && output == null)) return 6u;
                if (bytes.Length != 0) bytes.CopyTo(new global::System.Span<byte>(output, bytes.Length));
                return 0u;
            }
            private static PropertyState? State(void* context) => context == null ? null : GCHandle.FromIntPtr((nint)context).Target as PropertyState;
            private uint Failure(global::System.Exception error) { Application.__ReportCallbackException(error); return 8u; }

            [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
            private static uint GetCallback(void* context, PropertyValue* value, byte* output, ulong capacity, ulong* required)
            {
                var state = State(context); if (state is null || value == null || required == null) return 1u;
                if (global::System.Environment.CurrentManagedThreadId != state.owner.ownerThreadId) return 4u;
                try { *value = state.Encode(state.Descriptor.GetValue(state.owner.target), out var text); return Write(text, output, capacity, required); }
                catch (global::System.Exception error) { return state.Failure(error); }
            }
            [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
            private static uint SetCallback(void* context, PropertyValue* value)
            {
                var state = State(context); if (state is null || value == null) return 1u;
                if (global::System.Environment.CurrentManagedThreadId != state.owner.ownerThreadId) return 4u;
                try { ++state.SuppressNotifications; try { state.Descriptor.SetValue(state.owner.target, state.Decode(value)); } finally { --state.SuppressNotifications; } return 0u; }
                catch (global::System.Exception error) { return state.Failure(error); }
            }
            [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
            private static uint ResetCallback(void* context)
            {
                var state = State(context); if (state is null) return 1u;
                if (global::System.Environment.CurrentManagedThreadId != state.owner.ownerThreadId) return 4u;
                try { ++state.SuppressNotifications; try { state.Descriptor.ResetValue(state.owner.target); } finally { --state.SuppressNotifications; } return 0u; }
                catch (global::System.Exception error) { return state.Failure(error); }
            }
            [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
            private static uint ShouldSerializeCallback(void* context, uint* result)
            {
                var state = State(context); if (state is null || result == null) return 1u;
                if (global::System.Environment.CurrentManagedThreadId != state.owner.ownerThreadId) return 4u;
                try { *result = state.Descriptor.ShouldSerializeValue(state.owner.target) ? 1u : 0u; return 0u; }
                catch (global::System.Exception error) { return state.Failure(error); }
            }
            [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
            private static uint FormatCallback(void* context, PropertyValue* value, byte* output, ulong capacity, ulong* required)
            {
                var state = State(context); if (state is null || value == null || required == null) return 1u;
                if (global::System.Environment.CurrentManagedThreadId != state.owner.ownerThreadId) return 4u;
                try { var managed = state.Decode(value); var text = managed is null ? string.Empty : state.converter.ConvertToString(null, global::System.Globalization.CultureInfo.CurrentCulture, managed) ?? string.Empty; return Write(text, output, capacity, required); }
                catch (global::System.Exception error) { return state.Failure(error); }
            }
            [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
            private static uint ParseCallback(void* context, StringView input, PropertyValue* value, byte* output, ulong capacity, ulong* required)
            {
                var state = State(context); if (state is null || value == null || required == null) return 1u;
                if (global::System.Environment.CurrentManagedThreadId != state.owner.ownerThreadId) return 4u;
                try { var text = Read(input); object? parsed; if (text.Length == 0 && state.nullable) parsed = null; else if (state.payloadType == typeof(string)) parsed = text; else if (state.payloadType == typeof(char)) parsed = text.Length == 1 ? text[0] : throw new global::System.FormatException("Character properties require exactly one character."); else parsed = state.converter.ConvertFromString(null, global::System.Globalization.CultureInfo.CurrentCulture, text); *value = state.Encode(parsed, out var valueText); return Write(valueText, output, capacity, required); }
                catch (global::System.Exception error) { return state.Failure(error); }
            }

            [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
            private static uint EditCallback(void* context, PropertyValue* current, PropertyValue* edited, byte* output, ulong capacity, ulong* required)
            {
                var state = State(context); if (state is null || current == null || edited == null || required == null) return 1u;
                if (global::System.Environment.CurrentManagedThreadId != state.owner.ownerThreadId) return 4u;
                try
                {
                    if (state.editor is null || state.editorStyle == global::System.Drawing.Design.UITypeEditorEditStyle.None) return 5u;
                    var descriptorContext = new EditorContext(state.owner.target, state.Descriptor);
                    using var service = new EditorService(state.owner.ownerGrid);
                    var result = state.editor.EditValue(descriptorContext, service, state.Decode(current)!);
                    *edited = state.Encode(result, out var valueText);
                    return Write(valueText, output, capacity, required);
                }
                catch (global::System.Exception error) { return state.Failure(error); }
            }

            private sealed class EditorContext : global::System.ComponentModel.ITypeDescriptorContext
            {
                private readonly object instance;
                private readonly global::System.ComponentModel.PropertyDescriptor descriptor;
                internal EditorContext(object instance, global::System.ComponentModel.PropertyDescriptor descriptor) { this.instance = instance; this.descriptor = descriptor; }
                public global::System.ComponentModel.IContainer? Container => null;
                public object Instance => instance;
                public global::System.ComponentModel.PropertyDescriptor PropertyDescriptor => descriptor;
                public object? GetService(global::System.Type serviceType) => null;
                public void OnComponentChanged() { }
                public bool OnComponentChanging() => true;
            }

            private sealed class EditorService : global::System.IServiceProvider, global::System.Windows.Forms.Design.IWindowsFormsEditorService, global::System.IDisposable
            {
                private readonly PropertyGrid grid;
                private Form? dropDown;
                private bool disposed;
                internal EditorService(PropertyGrid grid) { this.grid = grid; }
                public object? GetService(global::System.Type serviceType) => serviceType == typeof(global::System.Windows.Forms.Design.IWindowsFormsEditorService) ? this : null;
                public void CloseDropDown() { if (dropDown is not null && !dropDown.IsDisposed) dropDown.Close(); }
                public void DropDownControl(Control control)
                {
                    if (disposed) throw new global::System.ObjectDisposedException(nameof(EditorService));
                    if (control is null) throw new global::System.ArgumentNullException(nameof(control));
                    if (dropDown is not null) throw new global::System.InvalidOperationException("A property editor drop-down is already active.");
                    if (control.Parent is not null) throw new global::System.ArgumentException("A property editor drop-down control must be unparented.", nameof(control));
                    var priorBounds = control.Bounds;
                    var width = global::System.Math.Clamp(priorBounds.Width > 0 ? priorBounds.Width : 240, 80, 1024);
                    var height = global::System.Math.Clamp(priorBounds.Height > 0 ? priorBounds.Height : 180, 40, 768);
                    using var host = new Form { Name = grid.Name + ".EditorDropDown", Text = string.Empty, FormBorderStyle = FormBorderStyle.FixedSingle, ShowInTaskbar = false, StartPosition = FormStartPosition.Manual, ClientSize = new global::System.Drawing.Size(width, height) };
                    var anchor = grid.PointToScreen(new global::System.Drawing.Point(global::System.Math.Max(0, grid.Width - width), grid.Height));
                    host.Location = anchor;
                    control.Bounds = new global::System.Drawing.Rectangle(0, 0, width, height);
                    host.Controls.Add(control);
                    dropDown = host;
                    try
                    {
                        var owner = grid.FindForm();
                        if (owner is null) host.ShowDialog(); else host.ShowDialog(owner);
                    }
                    finally
                    {
                        dropDown = null;
                        host.Controls.Remove(control);
                        if (!control.IsDisposed) control.Bounds = priorBounds;
                    }
                }
                public DialogResult ShowDialog(Form dialog)
                {
                    if (disposed) throw new global::System.ObjectDisposedException(nameof(EditorService));
                    if (dialog is null) throw new global::System.ArgumentNullException(nameof(dialog));
                    if (dropDown is not null) throw new global::System.InvalidOperationException("A modal property editor cannot open inside a drop-down transaction.");
                    var owner = grid.FindForm();
                    return owner is null ? dialog.ShowDialog() : dialog.ShowDialog(owner);
                }
                public void Dispose() { if (disposed) return; disposed = true; CloseDropDown(); }
            }

            public void Dispose()
            {
                if (changeHooked)
                {
                    Descriptor.RemoveValueChanged(owner.target, valueChanged);
                    changeHooked = false;
                }
                if (callbackRoot.IsAllocated) callbackRoot.Free();
            }
        }
    }

    private static Api LoadApi()
    {
        var value = new Api { StructSize = (uint)sizeof(Api) };
        Check(GetApi(26, ref value));
        if (value.AbiVersion != 26 || value.BeginInvoke == 0 || value.RequestClose == 0 || value.SetControlPng == 0 || value.SetChildIndex == 0 || value.SetControlColors == 0 || value.SubscribePointer == 0 || value.SetCheckState == 0 || value.GetCheckState == 0 || value.SubscribeKey == 0 || value.SubscribeText == 0 || value.SetRange == 0 || value.GetRange == 0 || value.SetRangeValue == 0 || value.GetRangeValue == 0 || value.SetPointerCapture == 0 || value.GetPointerCapture == 0 || value.ShowPathDialog == 0 || value.LastDialogPath == 0 || value.ShowTooltip == 0 || value.HideTooltip == 0 || value.SetFieldSelection == 0 || value.SetFieldEditState == 0 || value.FieldPositionFromPoint == 0 || value.WriteClipboardText == 0 || value.ReadClipboardText == 0 || value.FieldNavigate == 0 || value.FieldReplace == 0 || value.FieldHistory == 0 || value.FieldClearHistory == 0 || value.SetControlPixels == 0 || value.GetControlAbsoluteBounds == 0 || value.SubscribeKeyPreview == 0 || value.SetCursor == 0 || value.GetCursor == 0 || value.SetAutoScrollOffset == 0 || value.SetAutoScroll == 0 || value.SetAutoScrollMargin == 0 || value.SetAutoScrollMinSize == 0 || value.SetAutoScrollPosition == 0 || value.GetScrollState == 0 || value.SetScrollAxisState == 0 || value.ScrollControlIntoView == 0 || value.SuspendLayout == 0 || value.ResumeLayout == 0 || value.PerformControlLayout == 0 || value.GetLayoutState == 0 || value.PropertyGridSetSelectedControls == 0 || value.PropertyGridSetSort == 0 || value.PropertyGridGetSort == 0 || value.PropertyGridRefresh == 0 || value.PropertyObjectDefine == 0 || value.PropertyObjectNotifyChanged == 0 || value.PropertyGridTrySetText == 0 || value.PropertyGridResetProperty == 0 || value.PropertyGridActivateEditor == 0 || value.SetControlTextAlignment == 0 || value.SetButtonAppearance == 0 || value.SetPanelBorderStyle == 0 || value.AttachOverlay == 0 || value.DetachOverlay == 0) throw new InvalidOperationException("GUI.Forms ABI 0.26 table is incomplete.");
        return value;
    }
    private static void Check(int result)
    {
        if (result == 0) return;
        var detail = string.Empty;
        if (api.LastError != 0)
        {
            ErrorView error;
            var errorResult = ((delegate* unmanaged[Cdecl]<ErrorView*, int>)api.LastError)(&error);
            if (errorResult == 0 && error.Message.Data != null && error.Message.Size != 0)
                detail = Encoding.UTF8.GetString(error.Message.Data, checked((int)error.Message.Size));
        }
        if (Environment.GetEnvironmentVariable("GUI_FORMS_TRACE_ABI_ERRORS") == "1")
            Console.Error.WriteLine($"facade-abi-error=result:{result}|thread:{Environment.CurrentManagedThreadId}|detail:{detail}\n{Environment.StackTrace}");
        throw new InvalidOperationException($"GUI.Forms ABI operation failed with result {result}{(detail.Length == 0 ? "." : $": {detail}")}");
    }

    private sealed class SafeControlHandle
    {
        internal Handle Value { get; private set; }
        private bool released;
        internal SafeControlHandle(Handle value) { Value = value; }
        internal void DisposeNative() { if (released) return; Check(((delegate* unmanaged[Cdecl]<Handle, int>)api.Dispose)(Value)); released = true; GC.SuppressFinalize(this); }
        ~SafeControlHandle() { if (!released) _ = ((delegate* unmanaged[Cdecl]<Handle, int>)api.Release)(Value); }
    }

    private sealed class NativeAsyncResult : global::System.IAsyncResult
    {
        private readonly global::System.Delegate method;
        private readonly global::System.Threading.ManualResetEvent completed = new(false);
        private readonly string traceName;
        private readonly bool traceThis;
        private int executionState;
        private object? result;
        private Exception? error;
        internal NativeAsyncResult(global::System.Delegate method, int ownerThreadId)
        {
            this.method = method;
            OwnerThreadId = ownerThreadId;
            Id = global::System.Threading.Interlocked.Increment(ref nextAsyncId);
            var declaringType = method.Method.DeclaringType?.FullName ?? string.Empty;
            traceName = $"{declaringType}.{method.Method.Name}";
            traceThis = traceDelegates && (declaringType.StartsWith("retired compatibility specimen.FrontEnds.SpyServer", StringComparison.Ordinal) ||
                declaringType == "retired compatibility specimen.MainForm" && method.Method.Name == "HandleFrontendControllerSampleRateChange");
        }
        internal long Id { get; }
        internal int OwnerThreadId { get; }
        public object? AsyncState => null;
        public global::System.Threading.WaitHandle AsyncWaitHandle => completed;
        public bool CompletedSynchronously => false;
        public bool IsCompleted { get; private set; }
        internal void Execute()
        {
            if (global::System.Threading.Interlocked.CompareExchange(ref executionState, 1, 0) != 0) return;
            Untrack(this);
            if (traceThis) Console.Error.WriteLine($"facade-delegate=execute-begin|method={traceName}|thread={Environment.CurrentManagedThreadId}");
            try
            {
                // BeginInvoke(Action) is the overwhelmingly common UI path and
                // must not cross reflection's invocation trampoline. Besides
                // avoiding avoidable latency, direct typed invocation preserves
                // useful managed stacks through nested native callbacks and does
                // not misclassify reverse-P/Invoke thunks reached by the action.
                if (method is global::System.Action action) { action(); result = null; }
                else result = method.DynamicInvoke();
            }
            catch (Exception caught) { error = caught; throw; }
            finally
            {
                IsCompleted = true;
                completed.Set();
                if (traceThis) Console.Error.WriteLine($"facade-delegate=execute-end|method={traceName}|thread={Environment.CurrentManagedThreadId}");
            }
        }
        internal void Cancel() { if (global::System.Threading.Interlocked.CompareExchange(ref executionState, 1, 0) != 0) return; Untrack(this); error = new global::System.OperationCanceledException("GUI.Forms host closed before the queued invocation ran."); IsCompleted = true; completed.Set(); }
        internal object? GetResult() { if (error is not null) throw error; return result; }
    }
}
""";

internal sealed record ApiRow(string Id, string Assembly, string Type, string? Member, string? Signature, string MemberKind)
{
    public static ApiRow Read(JsonElement row) => new(row.GetProperty("id").GetString()!, row.GetProperty("targetAssembly").GetString()!,
        row.GetProperty("type").GetString()!, row.TryGetProperty("member", out var member) ? member.GetString() : null,
        row.TryGetProperty("signature", out var signature) ? signature.GetString() : null, row.GetProperty("memberKind").GetString()!);
}

internal sealed record Resolution(string Id, string RequestedAssembly, string Type, string? Member, string? Signature, string Status, string? ActualAssembly);
