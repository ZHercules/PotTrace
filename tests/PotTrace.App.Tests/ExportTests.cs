using PotTrace.App.Services;
namespace PotTrace.App.Tests;
public class ExportTests
{
    [Theory]
    [InlineData("=cmd.avi","\"'=cmd.avi\"")]
    [InlineData("a\"b,c\n.avi","\"a\"\"b,c\n.avi\"")]
    public void CsvEscapesCellsAndNeutralizesFormulas(string raw,string expected)=>Assert.Equal(expected,ExportService.Cell(raw));
}
