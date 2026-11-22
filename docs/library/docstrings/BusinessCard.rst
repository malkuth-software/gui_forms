The program's business card: its vendor, product and version, website and
copyright, on a card drawn through the theme, lying in a page or held in
the hand.

The card's text is the process's identity, set once with
`set_program_identity`: the vendor large, a rule, the product and version,
the website, and in the hand the copyright. Lying in a page (a help page, an
About pane), it casts its shadow and rises a little under the pointer; a
click, Enter or Space asks to open it in the hand. In the hand, alone over
the dimmed window, it tilts toward the pointer, a click on the website asks
for the website, and Escape, its close mark or a click off the card puts it
away. Use an `AboutDialog` for a standard About box with credits.

Parameters
----------
stable_id
    Identity of the control, unique within its window.
presentation
    Where the card is shown: `BusinessCardPresentation` ``page`` (the
    default) or ``hand``.

Properties
----------
presentation
    Where the card is shown.
last_website_request
    What became of the last request for the website
    (`BusinessCardLinkOutcome`): none yet, answered by a handler, opened by
    the host, unavailable (no open-URL capability, or no window), or
    refused.
heading_band_bottom
    The heading band's lower edge in this control's coordinates, or none
    without a band rule in force.
turn_available
    Whether this card can show its turn in the hand: it is in a window and
    its host lent an offscreen renderer for the faces at its last try.
turn
    The card's turn about its vertical axis in radians: 0 face up at rest,
    pi face down.
pitch
    The card's turn about its horizontal axis, within plus or minus
    `maximum_business_card_pitch`.
face_up
    Whether the card shows its front.
card_bounds
    The card's stock in this control's coordinates, before the tilt; the
    control keeps a margin around it for the shadow and the tilt.
vendor_name_band
    The band of the vendor name, before the tilt. `website_band` is the
    website line (empty without a website) and `close_mark_bounds` the close
    mark (in the hand only).
tilt
    The face's offset toward the pointer in logical units; zero with the
    pointer away or under reduced motion.
remark
    The newest remark around the card in the hand, or empty;
    `remark_count` counts those shown.

Events
------
open_requested
    Raised on the page when the card asks to be opened in the hand (a
    click, Enter or Space). A handler that opens it itself sets the
    request's ``handled``; unhandled, the card calls
    ``show_business_card`` on its window.
put_away
    Raised in the hand when the card is put away: Escape, its close mark, a
    click off it, or ``hide_business_card``.
website_requested
    Raised when the website is asked for (a click on it in the hand, or the
    press action there). A handler that opens it itself sets ``handled``;
    unhandled, the card hands the address to its window's host
    (`HostServices::open_url`).

Methods
-------
set_heading_band
    Lays the card on the page under a band: a control of the same window
    drawn over the page above the card, such as a help book's heading band,
    so that a page scrolled up carries the card under the band. The card and
    its shadow are drawn, and take the pointer, only below the band's lower
    edge. The card observes the band and never keeps it alive; null clears
    the rule. Ignored in the hand.

See Also
--------
AboutDialog : A standard About box.
WindowCapsule : The window controls whose help shows the card.

Notes
-----
`set_program_identity` validates the whole `ProgramIdentity` before it
replaces the previous one: text that is not valid UTF-8, or a website that
is neither empty nor an ``https://`` address, throws
`std::invalid_argument` and leaves the identity as it was. Empty fields are
left off the card. A card already shown shows a new identity when it next
paints.

A program may also give the card remarks (`set_business_card_remark_source`):
while the pointer is over the card in the hand, the card asks for a remark
a moment after the pointer arrives and then every one to two and a half
seconds, and shows each around the card, never on it, rising and fading,
at most `maximum_business_card_remarks` at once and
`maximum_business_card_remark` bytes each.

`set_diagnostic_overlay` turns on, for every window without a setting of its
own, the invalidation outlines and a counters card at the top left of each
window's overlay plane; `toggle_diagnostic_overlay` turns it to the other
state.

To assistive technology the card is named "<vendor> business card", with
the credits in its description; its press action opens it (on the page) or
asks for the website (in the hand), and in the hand its close action puts
it away. A theme draws it in the card family: ``shadow``, ``stock``,
``vendor_name``, ``rule``, ``product``, ``website``, ``copyright``,
``remark``, ``close_mark`` and ``focus``.
