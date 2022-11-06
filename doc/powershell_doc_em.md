# PowerShell documentation
_by Emanuele Messina_

<br>

## Command line

<br>

Refresh environment variables
```powershell
refreshenv
```
\
Equivalent of `grep`
```powershell
| Select-String "string"
```

<br>

---

<br>

## Script Parametes

<br>

Must be the first non commented line in the script
```powershell
param(
    [switch]$activateFunctionality = $false

    [Parameter(
        HelpMessage="Help message shown with Get-Help",
        <Mandatory,>
    )]
    [string] $stringParam = "default"
)
```
\
Parameters are called by name
```powershell
> script.ps1 -activateFunctionality -stringParam "mystring"
```

<br>

---

<br>

## Variables

<br>

- `$env:var_name` : access environment variable
- `$PSScriptRoot` : current script directory
- `$true` , `$false` : boolean values
- `$null` : null 
- `$?` : true or false depending on whether previous command exited without error or not
- `$lastexitcode`: exit code of last command

<br>

---

<br>

## Call executables

<br>

Use `&` to parse the path string, otherwise will not find the executable
```powershell
& "path/to/run.exe" -param1 arg1 ...
```

<br>

---

<br>


## Functions

<br>

```powershell
# decl
function func ($arg1, $arg2, ...){
   # code
}

#call
func arg1 arg2 ...
```
\
Access variables outside function

```powershell
$var = 5

function func ($arg){
    $script:var = ...
}
```

<br>

### Lambdas

<br>

```powershell
$lambda = { param($x) $x = ... }

$lambda.Invoke(5)
```

<br>

### References

<br>

>Useful for returning values and manipulating objects

```powershell
# decl
function func ($arg, [ref]$ref){
    $ref.Value = ... #access
}

# call
func $var ([ref]$out)
```

<br>

---

<br>

## Conditional statements

<br>

### **If**

<br>

Operators: `-and`,`-or`,`-not`, `-eq`, `-neq` ...

<br>

### **Switch**

<br>

Simple

```powershell
switch (4)
{
    1 {"It is one."; Break}
    2 {"It is two." ; Break }
    3 {"It is three." ; Break }
    4 {"It is four." ; Break }
    3 {"Three again."}
    Default{  }
}
```
\
Regex
```powershell
switch -regex <-file $file> # if file is specified every line is checked
{
    "regex1 with (first capt. grp.) (sec capt. grp.) ..."
    {
        $matches[1] # first capture group
        $matches[2] # second capture group and so on
        ...
        <Continue> # if continue, subsequent regex test are carried out
    }
    "regex2" { ... }
    ...
}
```

<br>

### **Try/Catch**

<br>

Catch an error from command
```powershell
try{
    I_Will_Crash ... -ErrorAction Stop
}
catch{
    # catch instead of crashing script
}
```

<br>

---

<br>

## Loops

<br>

### Foreach

<br>

```powershell
foreach($elem in $list){
    # ...
}
```

<br>

---

<br>

## Data structures

<br>

### **Enums**

<br>

#### Declaration

<br>

```powershell
enum myenum{
    element = 5
}
```

<br>

#### Access

<br>

Single
```powershell
[myenum]::element
```

<br>

---

<br>

### **Arrays**

<br>

Use inline
```powershell
(elem1, elem2, ...)
```

<br>

---

<br>

### **Lists**

<br>

***Must use this namespace***
```powershell
using namespace System.Collections.Generic
```

<br>

List of generic objects
```powershell
$objs = [List[PSObject]]::new();
```
\
Add element to list
```powershell
$objs.Add($obj)
```

<br>

---

<br>

### **Hashtables**

<br>

#### Declaration

<br>

```powershell
$environments = @{
    Prod = 'SrvProd05'
    QA   = 'SrvQA02'
    Dev  = 'SrvDev12'
}
```
\
**Ordered**
> By defaults hastables are unordered...
```powershell
[ordered]@{ ... }
```

<br>

#### Access

<br>

Single
```powershell
$server = $environments['QA']
$server = $environments.QA
```
\
Multiselection
```powershell
$environments[@('QA','DEV')]
$environments[('QA','DEV')]
$environments['QA','DEV']
```

<br>

---

<br>

## Strings

<br>

Variables in string

```powershell
 "$($var)"
```
\
Escape single 
```powershell
"this is `$(`"all text`")"
```
\
Escape all
```powershell
'this is $("all text")'
```

<br>

---

<br>

### **Regex**

<br>

Get matches in $str against the pattern (**global**)
```powershell
$m = [regex]::matches($str , '\*(\d)');
```
\
Get capture groups in matches
```powershell
foreach ($match in $m){
    $match.Groups[0] # the entire match
    $match.Groups[1] # the first capture group
    # ... etc
}
```

<br>

---

<br>

## Print

<br>

Simple
```powershell
 Write-Host "string"
```
No new line
```powershell
-NoNewline
```
Colors
```powershell
-ForegroundColor "red" -BackgroundColor "white"
```
\
Get default colors
```powershell
$foregroundColor = (get-host).ui.rawui.ForegroundColor
$backgroundColor = (get-host).ui.rawui.BackgroundColor
```
\
Silence output of command
```powershell
| Out-Null
```
\
Error output control
```poweshell
-ErrorAction <Continue|Stop|Ingore...>
```

<br>

## UI

<br>

Show MessageBox
```powershell
Add-Type -AssemblyName System.Windows.Forms
[System.Windows.Forms.MessageBox]::Show("message")
```


<br>

---

<br>

## Interactions

<br>

Wait for key press
```powershell
$host.ui.RawUI.ReadKey("NoEcho,IncludeKeyDown")
```
\
Check for PowerShell ISE
```powershell
if($psISE){ ... }
```

<br>

---

<br>

## Files

<br>

Check if file exists
```powershell
Test-Path -Path <PATH to FILE> -PathType Leaf
```
\
Create file
```powershell
New-Item file
```
\
Delete single
```powershell
Remove-Item file
```
\
Delete directory with contents
```powershell
Remove-Item -LiteralPath directory -Force -Recurse
```
\
Don't parse wildcards (use literally what comes after)
```powershell
-LiteralPath
```
\
Join paths
```powershell
Join-Path $path1 $path2
```
\
Create directory structure if not present
```powershell
[System.IO.Directory]::CreateDirectory("path\folder\subfolder")
```
\
Copy entire folder with contents to location
```powershell
Copy-Item "path\to\mydir" -Destination "dest\parentdir" -Recurse
# will be copied as -> dest\parentdir\mydir
```

<br>

### **Read from file**

<br>

Read as ASCII string
```powershell
Get-Content -Path "path\to\file"
```

<br>

---

<br>

### **Write to file**

<br>

Overwrite content
```powershell
Set-Content path "content"
```

<br>

---

<br>

## XML

<br>

### **Load**

<br>

Create XML object from path
```powershell
$xml = New-Object XML
$xml.Load($path)
```
Get NameSpace
```powershell
$ns = $xml.DocumentElement.NamespaceURI
```
Get NameSpaceManager (adds namespace `ns`)
```powershell
$nsm.Value = New-Object Xml.XmlNamespaceManager($xml.NameTable)
$nsm.Value.AddNamespace('ns', $ns)
```

<br>

### **Access**

<br>

Access elements of DOM (returns list of subnodes)
```powershell
$xml.ParentNode.ChildNode.SubNode # returns list of SubNode nodes
```
\
Other way to select nodes (not recommended, requires explicit use of NameSpaceManager)
```powershell
# single
$parent.SelectSingleNode("//ns:NodeName", $nsm) # returns first
# multiple
$parent.SelectNodes("//ns:NodeName", $nsm)
```
\
Select nodes based on attribute value (logical and string operators apply)
```powershell
$found = $nodes | Where-Object {$_.AttributeName -eq "value" } # returns list if multiple matches
```
\
Get OuterXml
```powershell
$node.OuterXml
```
<br>

### **Create**

<br>

Instantiate new node (must be done from the root `$xml` object in the same namespace `$ns`)
```powershell
$newNode = $xml.CreateElement("NodeName", $ns)
```

<br>

### **Modify**

<br>

Set Attribute
```powershell
$node.SetAttribute("AttributeName", "value")
```
\
Set `#text` property
```powershell
$node.'#text` = ...
```
\
Set Inner Text (applies to the innermost text even under many children)
\
```powershell
$node.InnerText = "text"
```
\
Set content of Node
```powershell
$parent.Node = "..."
```
\
Set InnerXml (insert namespace `xmlns=""` everywhere, not recommended)
```powershell
$node.InnerXml = "<node>... <other ... /> </node> ... "
```
\
Append Child
```powershell
$parent.AppendChild($child)
```
\
Remove Child
```powershell
$parentNode.RemoveChild($childNode)
```
\
Insert After
```powershell
$node.InsertAfter($newNode, $nodeBefore)
```

<br>

### **Save**

<br>

```powershell
$xml.Save($path)
```

<br>

---

<br>

