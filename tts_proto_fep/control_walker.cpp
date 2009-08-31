#include "control_walker.h"

#include <aknlists.h> 
#include <eikappui.h>
#include <eikedwin.h>
#include <eikenv.h>
#include <eiklabel.h>

#include "reporting.h"

namespace {
typedef void (CCoeControl::*DrawFunc)(const TRect& aRect) const;
}  // namespace

class CCoeRedrawer {
 public:
  inline static DrawFunc GetDraw(CCoeControl* ctrl) { return &ctrl->Draw; }
};

namespace {

// We want to be able detect known control class instances with the minimum
// fuss, so we we write the necessary reporting functions and generate a lookup
// table.
// For the technique http://blog.brush.co.nz/2009/08/xmacros/

void Report(CEikLabel* label, LoggingState* logger) {
  if (label->Text()) {
    logger->Log(*label->Text());
  } else {
    logger->Log(_L("[empty]"));
  }
}

void Report(CEikEdwin* edwin, LoggingState* logger) {
  HBufC* text = edwin->GetTextInHBufL();
  CleanupStack::PushL(text);
  logger->Log(*text);
  CleanupStack::PopAndDestroy(text);
}

void Report(CEikTextListBox* listbox, LoggingState* logger) {
  CTextListBoxModel* model = listbox->Model();
  TBuf<80> buf = _L("Item ");
  buf.AppendNum(listbox->CurrentItemIndex());
  buf.Append(_L(" of "));
  buf.AppendNum(model->NumberOfItems());
  buf.Append(_L(": "));
  const TDesC& text = model->ItemText(listbox->CurrentItemIndex());
  buf.Append(text.Left(50));
  logger->Log(text);
}

void Report(CAknGrid* grid, LoggingState* logger) {
  CTextListBoxModel* model = grid->Model();
  TBuf<80> buf = _L("Item ");
  buf.AppendNum(grid->CurrentItemIndex());
  buf.Append(_L(" of "));
  buf.AppendNum(model->NumberOfItems());
  buf.Append(_L(": "));
  const TDesC& text = model->ItemText(grid->CurrentItemIndex());
  buf.Append(text.Left(50));
  logger->Log(text);
}

struct ClassSig {
  const unsigned char* class_name;
  void* (*GetVTableAddress)(void);
  DrawFunc (*GetDrawAddress)(void);
  void (*ReportState)(CCoeControl* control, LoggingState* logger);
};

#define CONTROL(name) \
  void* Get ## name ## VTable() { \
    name * o = new (ELeave) name; \
    void* ptr = *(void**)o; \
    delete o; \
    return ptr; \
  } \
  DrawFunc Get ## name ## Draw() { \
    CCoeControl * o = new (ELeave) name; \
    DrawFunc ptr = CCoeRedrawer::GetDraw(o); \
    delete o; \
    return ptr; \
  } \
  void Report ## name ## State(CCoeControl* control, LoggingState* logger) { \
    name * o = reinterpret_cast< name * >(control); \
    Report(o, logger); \
  }
#include "control_classes.h"
#undef CONTROL

#define TEXT(name) (const unsigned char*)#name
#define CONTROL(name) \
  { TEXT(name), \
    & Get ## name ## VTable, \
    & Get ## name ## Draw, \
    & Report ## name ## State },

ClassSig classes[] = {
#include "control_classes.h"
  { 0 },
};
#undef CONTROL
#undef TEXT

void ReportWithSig(const ClassSig* control_class,
                   CCoeControl* control,
                   LoggingState* logger) {
  TBuf<100> name;
  name.Copy(TPtrC8(control_class->class_name));
  logger->Log(name);
  (*(control_class->ReportState))(control, logger);  
}

void InspectControl(CCoeControl* control, LoggingState* logger) {
  void* vtable = *(void**)control;
  for (const ClassSig* control_class = classes;
       control_class->class_name;
       ++control_class) {
    const bool vtable_match = vtable == (*(control_class->GetVTableAddress))();
    if (vtable_match) {
      ReportWithSig(control_class, control, logger);
      return;
    }
  }
#if 0
  int best_match_score = 0;
  const ClassSig* best_match = NULL;
  const int kDrawMethodSlot = 39 +4;
  for (const ClassSig* control_class = classes;
       control_class->class_name;
       ++control_class) {
    void** control_vtable = (void**)vtable;
    void** candidate_vtable = (void**)(*(control_class->GetVTableAddress))();
    const bool draw_method_match = control_vtable[kDrawMethodSlot] ==
                                   candidate_vtable[kDrawMethodSlot];
    if (draw_method_match) {
      ReportWithSig(control_class, control, logger);
      return;
    }
    /*
    int score = 0;
    // CCoeControl has 42 virtual functions (41 declared in CCoeControl
    // and a virtual destructor).
    for (int i = 0; i < 42; ++i) {
      if (*control_vtable == *candidate_vtable) ++score;
      ++control_vtable;
      ++candidate_vtable;
    }
    if (score > best_match_score) best_match = control_class;
    */
  }
  if (best_match) {
    ReportWithSig(best_match, control, logger);
  }
#endif
  logger->Log(_L("unknown"));
}

void WalkOne(CCoeControl* control, LoggingState* logger) {
  if (!control) return;
  InspectControl(control, logger);
  const int child_count = control->CountComponentControls();
  TBuf<100> buf = _L("pos ");
  buf.AppendNum(control->Position().iX);
  buf.Append(' ');
  buf.AppendNum(control->Position().iY);
  buf.Append(' ');
  buf.AppendNum(control->Size().iWidth);
  buf.Append(' ');
  buf.AppendNum(control->Size().iHeight);
  buf.Append(_L(" children "));
  buf.AppendNum(child_count);
  logger->Log(buf);
  if (child_count == 0) return;
  logger->IncreaseIndent();
  for (int i = 0; i < child_count; ++i) {
    WalkOne(control->ComponentControl(i), logger);
  }
  logger->DecreaseIndent();
}
}  // namespace

ControlWalker::ControlWalker() : CActive(CActive::EPriorityLow) {
  CActiveScheduler::Add(this);
}

ControlWalker::~ControlWalker() {
  Cancel();
}

void ControlWalker::TriggerWalk(LoggingState* logger) {
  logger_ = logger;
  TRequestStatus* s = &iStatus;
  User::RequestComplete(s, KErrNone);
  SetActive();
}

void ControlWalker::DoCancel() { }

void ControlWalker::RunL() {
  Walk(logger_);
}

void ControlWalker::Walk(LoggingState* logger) {
  CEikonEnv* env = CEikonEnv::Static();
  CCoeControl* top = env->AppUi()->TopFocusedControl();
  WalkOne(top, logger);
}
