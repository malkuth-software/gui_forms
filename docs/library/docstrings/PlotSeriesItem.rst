One series of a `PlotView`, declared as a child of the plot: what it draws,
its legend label, the axes it is drawn against, and where its data comes
from.

Data arrives in one of four ways, and each is a whole replacement: the last
call chooses.

- **Copied in**: `set_points`, `append_points` and `set_values` copy the
  data into columns the item owns. A series whose x never decreases is
  monotone; one that is also free of gaps keeps an envelope pyramid once it
  reaches `pyramid_threshold` points, so a large series draws at the cost of
  the plot's width.
- **A ring**: `use_ring`, then `push_points`, keeps the newest points up to
  the ring's capacity.
- **Borrowed**: `borrow_points` reads two leased views of the caller's
  arrays in place, under a pin each paint; the caller releases the lease
  before changing or freeing them (`plots::SeriesLease`).
- **Produced**: `source` names a signal program the plot runs on its clock,
  or `live_channel` gives a ring channel that a producer thread writes.

The item draws nothing, takes no input, no focus and no space, and publishes
no accessibility node: the plot reads it, and its legend entry toggles
`hidden`. Everything happens on the plot's UI thread, except a producer's
writes through the channel.

Parameters
----------
stable_id
    Identity of the item, unique within its window.

Properties
----------
kind : default ``PlotSeriesKind::line``
    What the series draws: a line (decimated to the plot's width), stairs,
    scatter markers, the shaded band between the series and zero, bars or
    stems.
label : default empty
    The series' name in the legend and to assistive technology.
source : default empty
    A signal program declaration, such as
    ``signal(sine, frequency 2Hz, amplitude 1)``
    (`plots::parse_signal_source`); empty for data written in C++. A
    declaration that does not parse throws `std::invalid_argument`. The
    program fills a ring of `signal_ring_capacity` points.
hidden : default ``false``
    Whether the series is left out of the plot; its legend entry stays,
    dimmed. Clicking the entry toggles it.
x_axis : default ``0``
    The horizontal axis the series is drawn against, by position among the
    plot's horizontal axes; past the last, the first.
y_axis : default ``0``
    The vertical axis the series is drawn against, by position among the
    plot's vertical axes; past the last, the first.
custom
    The custom series (`PlotCustomSeries`) installed with `set_custom`, or
    null. While one is installed the plot draws, fits and picks through it
    instead of the item's points and `kind`; null removes it. The item
    shares ownership.
data_source
    Whether the plot reads the item's columns or borrowed views.
column_samples
    The owned columns, as the plot reads them.
borrowed_x
    The borrowed x view; `borrowed_y` is the y view.
x_order
    Whether the series' x is monotone.
pyramid
    The envelope pyramid when it matches the columns (a growable, monotone,
    gap-free series of at least `pyramid_threshold` points), else null.
signal
    The parsed `source`, when one is set.
channel
    The live channel, once `live_channel` made it.
revision
    A count every data change and property write raises.
data_revision
    A count only changes to the points raise (a set, append, push, borrow or
    clear, a new ring, a signal or channel frame); a live plot copies a
    series again only when it moves.

Methods
-------
set_points
    Copies x and y values of equal length into owned columns; unequal
    lengths throw `std::invalid_argument`. It allocates, and nothing changes
    on a throw.
append_points
    Appends to the owned columns, after `set_points` or `set_values`, growing
    as a `std::vector` does. A monotone series stays monotone, or the append
    throws `std::invalid_argument` and changes nothing.
set_values
    Copies y values, with x = 0, 1, 2 and so on.
use_ring
    Replaces the data with an empty ring of a capacity of at least 2
    points, monotone in x; a smaller capacity throws
    `std::invalid_argument`.
push_points
    Pushes points into the ring, dropping the oldest past its capacity.
    Without `use_ring` first it throws `std::logic_error`; a point that goes
    back in x throws `std::invalid_argument` and changes nothing.
borrow_points
    Reads two borrowed views (`plots::BorrowedSeries`) in place from now on,
    with their x order. Invalid views throw `std::invalid_argument`.
live_channel
    The channel a producer thread writes (x, y) samples into, made with the
    given capacity on the first call and kept. The series becomes a ring of
    the channel's capacity, fed each frame.
clear_points
    Empties the series; copied-in columns keep their storage.
custom_changed
    Tells the plot that the installed custom series' content changed; it
    fits and repaints as for new points.
run_signal_frame
    Runs the signal program for a frame into the ring, with the plot's
    reused scratch; false when the item has no signal. The plot calls it.
consume_channel
    Moves the channel's published samples into the ring, through scratch
    storage of twice the capacity in doubles, and returns the count taken;
    0 without a channel. The plot calls it.

See Also
--------
PlotView : The plot that draws the series.
PlotAxisItem : The axes the series is drawn against.
PlotAnnotationItem : A label at a data point.
