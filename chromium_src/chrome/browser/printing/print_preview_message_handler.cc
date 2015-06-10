// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/printing/print_preview_message_handler.h"

#include <vector>

#include "atom/browser/ui/file_dialog.h"
#include "atom/browser/native_window.h"
#include "base/bind.h"
#include "base/json/json_reader.h"
#include "base/memory/ref_counted.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/shared_memory.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/printing/print_job_manager.h"
#include "chrome/browser/printing/printer_query.h"
#include "chrome/common/print_messages.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "printing/page_size_margins.h"
#include "printing/print_job_constants.h"
#include "printing/pdf_metafile_skia.h"

#include "atom/common/node_includes.h"

using content::BrowserThread;
using content::WebContents;

DEFINE_WEB_CONTENTS_USER_DATA_KEY(printing::PrintPreviewMessageHandler);

namespace {

void StopWorker(int document_cookie) {
  if (document_cookie <= 0)
    return;
  scoped_refptr<printing::PrintQueriesQueue> queue =
      g_browser_process->print_job_manager()->queue();
  scoped_refptr<printing::PrinterQuery> printer_query =
      queue->PopPrinterQuery(document_cookie);
  if (printer_query.get()) {
    BrowserThread::PostTask(BrowserThread::IO, FROM_HERE,
                            base::Bind(&printing::PrinterQuery::StopWorker,
                                       printer_query));
  }
}

base::RefCountedBytes* GetDataFromHandle(base::SharedMemoryHandle handle,
                                         uint32 data_size) {
  scoped_ptr<base::SharedMemory> shared_buf(
      new base::SharedMemory(handle, true));
  if (!shared_buf->Map(data_size)) {
    NOTREACHED();
    return NULL;
  }

  unsigned char* data_begin = static_cast<unsigned char*>(shared_buf->memory());
  std::vector<unsigned char> data(data_begin, data_begin + data_size);
  return base::RefCountedBytes::TakeVector(&data);
}

}  // namespace

namespace printing {


PrintPreviewMessageHandler::PrintPreviewMessageHandler(
    WebContents* web_contents)
    : content::WebContentsObserver(web_contents) {
  DCHECK(web_contents);
}

PrintPreviewMessageHandler::~PrintPreviewMessageHandler() {
}

void PrintPreviewMessageHandler::OnMetafileReadyForPrinting(
    const PrintHostMsg_DidPreviewDocument_Params& params) {
  // Always try to stop the worker.
  StopWorker(params.document_cookie);

  if (params.expected_pages_count <= 0) {
    NOTREACHED();
    return;
  }

  base::RefCountedBytes *data = (
      GetDataFromHandle(params.metafile_data_handle, params.data_size));
  RunPrintToPDFCallback(params.preview_request_id, data);
}

void PrintPreviewMessageHandler::OnPrintPreviewFailed(int document_cookie,
                                                      int request_id) {
  StopWorker(document_cookie);
  RunPrintToPDFCallback(request_id, nullptr);
}

bool PrintPreviewMessageHandler::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(PrintPreviewMessageHandler, message)
    IPC_MESSAGE_HANDLER(PrintHostMsg_MetafileReadyForPrinting,
                        OnMetafileReadyForPrinting)
    IPC_MESSAGE_HANDLER(PrintHostMsg_PrintPreviewFailed,
                        OnPrintPreviewFailed)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void PrintPreviewMessageHandler::PrintToPDF(
    const base::DictionaryValue& options,
    const atom::api::WebContents::PrintToPDFCallback& callback) {
  int request_id;
  options.GetInteger(printing::kPreviewRequestID, &request_id);
  print_to_pdf_callback_map_[request_id] = callback;

  content::RenderViewHost* rvh = web_contents()->GetRenderViewHost();
  rvh->Send(new PrintMsg_PrintPreview(rvh->GetRoutingID(), options));
}

void PrintPreviewMessageHandler::RunPrintToPDFCallback(
     int request_id, base::RefCountedBytes* data) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();
  v8::Locker locker(isolate);
  v8::HandleScope handle_scope(isolate);
  if (data) {
    v8::Local<v8::Value> buffer = node::Buffer::Use(
        const_cast<char*>(reinterpret_cast<const char*>(data->front())),
        data->size());
    print_to_pdf_callback_map_[request_id].Run(buffer);
  } else {
    print_to_pdf_callback_map_[request_id].Run(v8::Null(isolate));
  }
  print_to_pdf_callback_map_.erase(request_id);
}

}  // namespace printing
