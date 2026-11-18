using System.Security.Cryptography;
using System.Text;
namespace PotTrace.Core;
public static class MovieIdentity
{
    public static string GetKey(string path)=>Path.GetFileName(path.Replace('/', '\\')).ToUpperInvariant();
    public static Guid SourceId(string path,string? version=null)=>new(SHA256.HashData(Encoding.UTF8.GetBytes(path.ToUpperInvariant()+"\n"+version)).AsSpan(0,16));
}
