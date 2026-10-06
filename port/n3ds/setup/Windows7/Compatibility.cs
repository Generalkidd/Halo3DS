// Only APIs available on Windows 7 and .NET Framework 3.5 are used here.
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.IO;
using System.IO.Compression;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Web.Script.Serialization;

namespace HaloSetup
{
    public sealed class OperationCanceledException : Exception { public OperationCanceledException() : base("Setup was cancelled. Run setup again to finish any remaining files.") {} }
    public sealed class CancellationTokenSource : IDisposable
    {
        internal volatile bool Cancelled;
        public CancellationToken Token { get { return new CancellationToken(this); } }
        public void Cancel() { Cancelled = true; }
        public void Dispose() {}
    }
    public struct CancellationToken
    {
        readonly CancellationTokenSource source;
        internal CancellationToken(CancellationTokenSource value) { source = value; }
        public void ThrowIfCancellationRequested() { if (source != null && source.Cancelled) throw new OperationCanceledException(); }
    }
    public interface IProgress<T> { void Report(T value); }
    public static class LegacyEnvironment
    {
        static readonly System.Diagnostics.Stopwatch clock = System.Diagnostics.Stopwatch.StartNew();
        public static long TickCount64 { get { return clock.ElapsedMilliseconds; } }
    }
    public static class LegacyConvert
    {
        public static string ToHexStringLower(byte[] value) { return BitConverter.ToString(value).Replace("-", "").ToLowerInvariant(); }
    }
    public sealed class IncrementalHash : IDisposable
    {
        readonly SHA256 hash = new SHA256Managed();
        public static IncrementalHash CreateHash(HashAlgorithmName ignored) { return new IncrementalHash(); }
        public void AppendData(byte[] data, int offset, int count) { hash.TransformBlock(data, offset, count, data, offset); }
        public byte[] GetHashAndReset() { hash.TransformFinalBlock(new byte[0], 0, 0); byte[] result = hash.Hash; hash.Initialize(); return result; }
        public void Dispose() { ((IDisposable)hash).Dispose(); }
    }
    public struct HashAlgorithmName { public static HashAlgorithmName SHA256 { get { return new HashAlgorithmName(); } } }
    public sealed class JsonSerializerOptions { public bool WriteIndented { get; set; } }
    public static class JsonSerializer
    {
        static JavaScriptSerializer Create() { return new JavaScriptSerializer { MaxJsonLength = 8 * 1024 * 1024, RecursionLimit = 128 }; }
        public static T Deserialize<T>(Stream stream) { using (var reader = new StreamReader(stream)) return Create().Deserialize<T>(reader.ReadToEnd()); }
        public static byte[] SerializeToUtf8Bytes(object value, JsonSerializerOptions ignored) { return Encoding.UTF8.GetBytes(Create().Serialize(value)); }
    }
    public static class LegacyCompression
    {
        public static Stream OpenZlib(Stream source)
        {
            int cmf = source.ReadByte(), flg = source.ReadByte();
            Data.Require(cmf >= 0 && flg >= 0 && (cmf & 15) == 8 && (cmf >> 4) <= 7 &&
                ((cmf << 8) + flg) % 31 == 0 && (flg & 32) == 0, "The map has an invalid zlib header.");
            // Framework 3.5 DeflateStream takes raw DEFLATE. The complete compressed
            // input and expanded output are independently SHA-256 verified by Installer.
            return new DeflateStream(source, CompressionMode.Decompress, true);
        }
    }
    public struct ByteSlice
    {
        internal readonly byte[] Data;
        internal readonly int Offset, Count;
        public ByteSlice(byte[] data, int offset, int count)
        { if (offset < 0 || count < 0 || offset > data.Length - count) throw new ArgumentOutOfRangeException(); Data = data; Offset = offset; Count = count; }
        public bool SequenceEqual(byte[] other) { return SequenceEqual(new ByteSlice(other, 0, other.Length)); }
        public bool SequenceEqual(ByteSlice other)
        { if (Count != other.Count) return false; for (int i = 0; i < Count; i++) if (Data[Offset+i] != other.Data[other.Offset+i]) return false; return true; }
        public void CopyTo(byte[] target) { Array.Copy(Data, Offset, target, 0, Count); }
    }
    public static class BinaryPrimitives
    {
        public static uint ReadUInt32LittleEndian(ByteSlice b) { if (b.Count < 4) throw new ArgumentException(); return (uint)(b.Data[b.Offset] | b.Data[b.Offset+1]<<8 | b.Data[b.Offset+2]<<16 | b.Data[b.Offset+3]<<24); }
        public static ushort ReadUInt16LittleEndian(ByteSlice b) { if (b.Count < 2) throw new ArgumentException(); return (ushort)(b.Data[b.Offset] | b.Data[b.Offset+1]<<8); }
        public static void WriteUInt32LittleEndian(ByteSlice b, uint value) { if (b.Count < 4) throw new ArgumentException(); for (int i=0;i<4;i++) b.Data[b.Offset+i]=(byte)(value>>(8*i)); }
    }
    public static class Extensions
    {
        public static ByteSlice AsSpan(this byte[] data, int offset) { return new ByteSlice(data, offset, data.Length-offset); }
        public static ByteSlice AsSpan(this byte[] data, int offset, int count) { return new ByteSlice(data, offset, count); }
        public static void ReadExactly(this Stream stream, byte[] data)
        { int position=0; while (position<data.Length) { int n=stream.Read(data,position,data.Length-position); if(n==0) throw new EndOfStreamException("A source file is incomplete or was disconnected."); position+=n; } }
        public static int Read(this Stream stream, byte[] data) { return stream.Read(data,0,data.Length); }
        public static void Write(this Stream stream, byte[] data) { stream.Write(data,0,data.Length); }
        [DllImport("kernel32.dll", SetLastError=true)] static extern bool FlushFileBuffers(Microsoft.Win32.SafeHandles.SafeFileHandle handle);
        public static void Flush(this FileStream stream, bool disk)
        { stream.Flush(); if(disk && !FlushFileBuffers(stream.SafeFileHandle)) throw new Win32Exception(Marshal.GetLastWin32Error(), "Could not flush the destination. Check the card connection."); }
        public static bool TryAdd<K,V>(this Dictionary<K,V> dict,K key,V value) { if(dict.ContainsKey(key))return false; dict.Add(key,value);return true; }
        public static bool TryPop<T>(this Stack<T> stack,out T value) { if(stack.Count==0){value=default(T);return false;}value=stack.Pop();return true; }
        public static void Deconstruct<K,V>(this KeyValuePair<K,V> pair,out K key,out V value) { key=pair.Key;value=pair.Value; }
        public static IEnumerable<T> Append<T>(this IEnumerable<T> items,T item) { foreach(T value in items)yield return value;yield return item; }
    }
    public static class LegacyPath
    {
        public static char DirectorySeparatorChar { get { return System.IO.Path.DirectorySeparatorChar; } }
        public static string Combine(params string[] parts) { string value=parts[0];for(int i=1;i<parts.Length;i++)value=System.IO.Path.Combine(value,parts[i]);return value; }
        public static string GetFullPath(string p) { return System.IO.Path.GetFullPath(p); }
        public static string GetDirectoryName(string p) { return System.IO.Path.GetDirectoryName(p); }
        public static string GetFileName(string p) { return System.IO.Path.GetFileName(p); }
        public static string GetPathRoot(string p) { return System.IO.Path.GetPathRoot(p); }
        public static bool IsPathRooted(string p) { return System.IO.Path.IsPathRooted(p); }
        public static bool EndsInDirectorySeparator(string p) { return p.EndsWith("\\") || p.EndsWith("/"); }
        public static string TrimEndingDirectorySeparator(string p) { return p.Length > GetPathRoot(p).Length ? p.TrimEnd('\\','/') : p; }
    }
    public static class LegacyDirectory
    {
        public static bool Exists(string p) { return System.IO.Directory.Exists(p); }
        public static DirectoryInfo CreateDirectory(string p) { return System.IO.Directory.CreateDirectory(p); }
        public static DirectoryInfo GetParent(string p) { return System.IO.Directory.GetParent(p); }
        public static string[] EnumerateDirectories(string p) { return System.IO.Directory.GetDirectories(p); }
        public static string[] EnumerateFiles(string p) { return System.IO.Directory.GetFiles(p); }
        public static void Delete(string p,bool recurse) { System.IO.Directory.Delete(p,recurse); }
    }
    public static class LegacyFile
    {
        public static bool Exists(string p) { return System.IO.File.Exists(p); }
        public static FileStream OpenRead(string p) { return System.IO.File.OpenRead(p); }
        public static byte[] ReadAllBytes(string p) { return System.IO.File.ReadAllBytes(p); }
        public static FileAttributes GetAttributes(string p) { return System.IO.File.GetAttributes(p); }
        public static void Delete(string p) { System.IO.File.Delete(p); }
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern bool MoveFileEx(string source,string target,uint flags);
        public static void Move(string a,string b) { System.IO.File.Move(a,b); }
        public static void Move(string a,string b,bool overwrite)
        { if (!overwrite) { Move(a,b);return; } if(!MoveFileEx(a,b,1|8))throw new Win32Exception(Marshal.GetLastWin32Error(),"Could not replace the destination file. Check the card connection and write protection."); }
    }
}
