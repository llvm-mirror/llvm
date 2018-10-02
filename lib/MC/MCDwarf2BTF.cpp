

#include "MCDwarf2BTF.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCObjectStreamer.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSectionELF.h"
#include "llvm/MC/MCBTFContext.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/EndianStream.h"
#include <fstream>

using namespace llvm;

void MCDwarf2BTF::addFiles(MCObjectStreamer *MCOS, std::string &FileName,
  std::vector<FileContent> &Files) {
  std::vector<std::string> Content;

  std::ifstream inputfile(FileName);
  std::string line;
  Content.push_back(line); // line 0 for empty string
  while (std::getline(inputfile, line))
    Content.push_back(line);

  Files.push_back(FileContent(FileName, Content));
}

void MCDwarf2BTF::addLines(MCObjectStreamer *MCOS, StringRef &SectionName,
  std::vector<FileContent> &Files,
  const MCLineSection::MCDwarfLineEntryCollection &LineEntries) {
  MCContext &context = MCOS->getContext();
  MCBTFContext *BTFCxt = context.getBTFContext();

  std::vector<BTFLineInfoSection> &LineInfoTable = BTFCxt->LineInfoTable;
  BTFLineInfoSection InfoSec;

  InfoSec.secname_off = BTFCxt->StringTable.addString(SectionName.str());

  for (const MCDwarfLineEntry &LineEntry : LineEntries) {
    BTFLineInfo LineInfo;
    unsigned FileNum = LineEntry.getFileNum();
    unsigned Line = LineEntry.getLine();

    LineInfo.label = LineEntry.getLabel();
    if (FileNum < Files.size()) {
      LineInfo.filename_off = BTFCxt->StringTable.addString(Files[FileNum].first);
      if (Line < Files[FileNum].second.size())
        LineInfo.line_off = BTFCxt->StringTable.addString(Files[FileNum].second[Line]);
      else
        LineInfo.line_off = 0;
    } else {
      LineInfo.filename_off = 0;
      LineInfo.line_off = 0;
    }
    LineInfo.line_num = Line;
    LineInfo.column_num = LineEntry.getColumn();
    InfoSec.info.push_back(std::move(LineInfo));
  }
  LineInfoTable.push_back(std::move(InfoSec));
}

void MCDwarf2BTF::addDwarfLineInfo(MCObjectStreamer *MCOS) {
  MCContext &context = MCOS->getContext();
  // no need for the rest if no type information
  if (context.getBTFContext() == nullptr)
    return;

  auto &LineTables = context.getMCDwarfLineTables();
  if (LineTables.empty())
    return;


  for (const auto &CUIDTablePair : LineTables) {
    std::vector<std::string> Dirs;
    std::vector<FileContent> Files;

    for (auto &Dir : CUIDTablePair.second.getMCDwarfDirs())
      Dirs.push_back(Dir);
    for (auto &File : CUIDTablePair.second.getMCDwarfFiles()) {
      std::string FileName;
      if (File.DirIndex == 0)
        FileName = File.Name;
      else
        FileName = Dirs[File.DirIndex - 1] + "/" + File.Name;
      MCDwarf2BTF::addFiles(MCOS, FileName, Files);
    }
    for (const auto &LineSec: CUIDTablePair.second.getMCLineSections().getMCLineEntries()) {
      MCSection *Section = LineSec.first;
      const MCLineSection::MCDwarfLineEntryCollection &LineEntries = LineSec.second;

      StringRef SectionName;
      if (MCSectionELF *SectionELF = dyn_cast<MCSectionELF>(Section))
        SectionName = SectionELF->getSectionName();
      else
        return;
      MCDwarf2BTF::addLines(MCOS, SectionName, Files, LineEntries);
    }
  }
}
