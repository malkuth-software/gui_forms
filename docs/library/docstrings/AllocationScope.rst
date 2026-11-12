Counts what GUI.Forms' own counted paths allocate on one thread around a workload.

Make an `AllocationScope` before a workload (a frame, a scroll, a setup
step) and read `counted` after it. The result gives, for each subsystem, the
allocations, releases and bytes that GUI.Forms made through its counted paths
on the constructing thread in between. Use `allocation_statistics` for the
same counts over every thread since the process started.

Properties
----------
counted
    What the constructing thread allocated and released through the counted
    paths since the scope was made or restarted. Throws `std::logic_error` on
    another thread.

Methods
-------
restart
    Moves the start to now, as a new scope would.

See Also
--------
AllocationStatistics : The per-subsystem counts and their JSON form.
ImageAtlas : A counted subsystem that allocates nothing after construction.

Notes
-----
Only GUI.Forms' counted paths are counted, each under a named subsystem: the
bytes the image registry holds for its images (`image_resources`), an image
atlas's tables (`image_atlas`), effect state storage (`effects`), the command
lists of display chunks and of the recording painter (`display_chunks`),
damage rectangles and pending paint marks (`damage_lists`), and the paragraph
layout's scratch (`text_layout`, once that code counts). GUI.Forms does not
replace or hook the global `operator new`, so the program's allocations,
other containers inside GUI.Forms, Skia, FreeType, HarfBuzz and the host
toolkits are not counted. The figures are never the process total.

Bytes are the bytes asked of `operator new`, without allocator overhead. A
release is counted on the thread that releases. Scopes nest freely and hold
no resources. The JSON form of the counts carries
`"scope":"gui_forms_counted_paths"` so that a record is not read as a process
figure.
