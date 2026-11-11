# SPDX-License-Identifier: AGPL-3.0-only
# Dot-source this, then call [AttachmentScan]::Save(path, sourceUrl). It hands a
# file to Windows' attachment service (IAttachmentExecute::Save) the way Chrome
# does after a download; the service calls the antivirus. Returns the HRESULT
# as text: 0x00000000 = accepted, 0x800700E1 or 0x80004005 = refused (the
# antivirus usually removes the file too).
Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;
[ComImport, Guid("73db1241-1e85-4581-8e4f-a81e1d0f8c57"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface IAttachmentExecute {
  [PreserveSig] int SetClientTitle([MarshalAs(UnmanagedType.LPWStr)] string title);
  [PreserveSig] int SetClientGuid(ref Guid guid);
  [PreserveSig] int SetLocalPath([MarshalAs(UnmanagedType.LPWStr)] string path);
  [PreserveSig] int SetFileName([MarshalAs(UnmanagedType.LPWStr)] string name);
  [PreserveSig] int SetSource([MarshalAs(UnmanagedType.LPWStr)] string source);
  [PreserveSig] int SetReferrer([MarshalAs(UnmanagedType.LPWStr)] string referrer);
  [PreserveSig] int CheckPolicy();
  [PreserveSig] int Prompt(IntPtr hwnd, int prompt, out int action);
  [PreserveSig] int Save();
}
[ComImport, Guid("4125dd96-e03a-4103-8f70-e0597d803b9c")]
public class AttachmentServices {}
public static class AttachmentScan {
  public static string Save(string path, string source) {
    var services = (IAttachmentExecute)new AttachmentServices();
    var client = new Guid("2676A9A2-D919-4FEE-9187-152100393AB2"); // Chrome's client GUID
    services.SetClientGuid(ref client);
    services.SetLocalPath(path);
    services.SetSource(source);
    int hr = services.Save();
    return "0x" + hr.ToString("X8");
  }
}
"@
