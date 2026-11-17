The result of converting RTF to markdown or markdown to RTF.

`import_rtf` reads RTF (the RTF 1.9.1 subset stx_rtf2md reads) and writes
the markdown dialect's canonical text; `export_rtf` reads the dialect with
stx_rtf2md's normative reader and writes canonical RTF, which `import_rtf`
reads back to the same document. A result is imported, with its losses and
the body size its sizes are relative to; exported, with its losses; or
failed, with an error and the input offset where reading stopped. A failed
conversion leaves its output empty.

See Also
--------
MarkdownDocument : Markdown parsed for presentation and editing.
MarkdownLossReport : What a conversion could not keep.

Notes
-----
RTF can say more than the dialect: pictures, embedded objects, fonts,
indents and spacing are dropped and counted. Markdown outside the dialect
(raw HTML, images, tables, task markers, rules) is kept as text and
counted. `describe_loss` gives one sentence for a reader per kind.

Examples
--------
Importing an RTF document and listing what it lost:

.. code-block:: cpp

   const std::string_view text = "{\\rtf1\\ansi {\\b Bold} text\\par}";
   const std::span<const std::byte> rtf(reinterpret_cast<const std::byte*>(text.data()), text.size());
   std::string markdown;
   const gf::RtfConversionResult imported = gf::import_rtf(rtf, markdown);   // "**Bold** text\n"
   std::string summary;
   if (imported.succeeded()) {
       const stxlib::Optional<gf::MarkdownLossReport> reported = imported.losses();
       const gf::MarkdownLossReport losses = *reported.value();
       for (std::size_t kind = 0; kind < gf::markdown_loss_kind_count; ++kind) {
           const gf::MarkdownLoss loss = static_cast<gf::MarkdownLoss>(kind);
           summary += gf::describe_loss(loss, losses.count(loss));
       }
   }
