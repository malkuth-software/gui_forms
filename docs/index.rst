.. _gui_forms_docs_mainpage:

#######################
GUI.Forms documentation
#######################

.. toctree::
   :maxdepth: 1
   :hidden:

   User Guide <user/index>
   API reference <api/index>
   Building from source <building/index>
   Development <dev/index>
   Release notes <release/index>


**Version**: |version|

**Download programs**:
`Latest release <https://github.com/malkuth-software/gui_forms/releases/latest>`_

**Useful links**:
`Source Repository <https://github.com/malkuth-software/gui_forms>`_ |
`Issue Tracker <https://github.com/malkuth-software/gui_forms/issues>`_ |
`Releases <https://github.com/malkuth-software/gui_forms/releases>`_

**What's new**:
:doc:`Unreleased (towards 1.0) <release/unreleased>` |
:doc:`0.5, the ABI and API seal <release/0.5>` |
:doc:`Roadmap <dev/roadmap>`

GUI.Forms is a retained, CPU-rendered forms toolkit written in C++20. You
build a tree of controls once, change it imperatively, and the toolkit tracks
exactly which layout, paint and hit-test work each change requires. It
provides stock controls in the Windows Forms tradition (buttons, text boxes,
lists, trees, tabs, splitters, menus, layout panels, bindings and timers),
native hosts for macOS, Windows and Linux, one text engine with identical
output on every platform, and an installed CMake SDK.

.. grid:: 1 1 2 2
    :gutter: 2 3 4 4

    .. grid-item-card::
        :img-top: _static/index-images/getting_started.svg
        :text-align: center

        Getting started
        ^^^

        New to GUI.Forms? Your first window takes one class and one
        function. It introduces controls, ownership and event handlers, and
        links to the rest of the guide.

        +++

        .. button-ref:: user/first-window
            :expand:
            :color: secondary
            :click-parent:

            To your first window

    .. grid-item-card::
        :img-top: _static/index-images/user_guide.svg
        :text-align: center

        User guide
        ^^^

        The user guide explains the key concepts of GUI.Forms: the retained
        tree, update scopes, layout, painting, text, input, threading and
        hosts.

        +++

        .. button-ref:: user/index
            :expand:
            :color: secondary
            :click-parent:

            To the user guide

    .. grid-item-card::
        :img-top: _static/index-images/api.svg
        :text-align: center

        API reference
        ^^^

        The reference describes every stock control and library type: what
        it is for, its constructor, properties, events and methods, with
        examples. It assumes you know the key concepts.

        +++

        .. button-ref:: api/index
            :expand:
            :color: secondary
            :click-parent:

            To the reference guide

    .. grid-item-card::
        :img-top: _static/index-images/contributor.svg
        :text-align: center

        Contributor's guide
        ^^^

        Want to add to the codebase or the documentation? The contributing
        guidelines cover the project's boundaries, house style, tests and
        evidence.

        +++

        .. button-ref:: dev/index
            :expand:
            :color: secondary
            :click-parent:

            To the contributor's guide
