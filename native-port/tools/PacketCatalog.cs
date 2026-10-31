// Migration tool: inspect the exact protocol assembly tracked by this checkout.
using System;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using OpenMetaverse.Packets;
static class Program {
    static void Main(){
        var catalog=Enum.GetValues<PacketType>().Where(value=>value!=PacketType.Default).Select(value=>{
            var packet=Packet.BuildPacket(value);
            return new {
                name=value.ToString(),enumValue=(int)value,id=packet.Header.ID,
                frequency=packet.Header.Frequency.ToString(),reliable=packet.Header.Reliable,
                zerocoded=packet.Header.Zerocoded,validateIds=packet.NeedValidateIDs,
                variableBlocks=packet.HasVariableBlocks,
                blocks=packet.GetType().GetFields(BindingFlags.Instance|BindingFlags.Public|BindingFlags.DeclaredOnly)
                    .Select(field=>new {name=field.Name,type=field.FieldType.Name,
                        fields=(field.FieldType.IsArray?field.FieldType.GetElementType():field.FieldType)
                            .GetFields(BindingFlags.Instance|BindingFlags.Public|BindingFlags.DeclaredOnly)
                            .Select(f=>new{name=f.Name,type=f.FieldType.Name}).ToArray()}).ToArray()
            };
        }).ToArray();
        Console.WriteLine(JsonSerializer.Serialize(catalog,new JsonSerializerOptions{WriteIndented=true}));
    }
}
