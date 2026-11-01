from pathlib import Path

path = Path(__file__).parent.parent / "Source/QuarryWorks/Private/Protocol/quarryworks.pb.cc"
text = path.read_text(encoding="utf-8-sig")
text = text.replace('#include "quarryworks.pb.h"', '#include "Protocol/QuarryProtobuf.h"', 1)
path.write_text(text, encoding="utf-8")
