# Copyright 2015 University of Rochester
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
# http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License. 


library(plyr)
library(ggplot2)

scientific_10 <- function(x) {
  parse(text=gsub("1e\\+", "10^", scales::scientific_format()(x)))
}
tests<-c("recovery")
for (t in tests){
read.csv(paste("./graph_",t,".csv",sep=""))->montagedata

montagedata$ds<-as.factor(gsub("TGraphRecovery","DRAM (T)",montagedata$ds))

montagedata$ds<-as.factor(gsub("MontageCreation","Montage (T)",montagedata$ds))
montagedata$ds<-as.factor(gsub("MontageRecovery","Montage",montagedata$ds))

ddply(.data=montagedata,.(ds,thread),mutate,latency= mean(latency)/1000)->lkdata

lindata = rbind(lkdata[,c("ds","thread","latency")])
lindata$ds <- factor(lindata$ds, levels=c("DRAM (T)", "NVM (T)", "Montage (T)", "Montage", "SOFT", "Dalí", "MOD", "Pronto-Full", "Pronto-Sync", "Mnemosyne"))

# Set up colors and shapes (invariant for all plots)
color_key = c("#12E1EA","#1245EA",
              "#FF69B4","#C11B14",
              "#660099","#1BC40F","#5947ff",
              "#FF8C00", "#F86945",
              "#191970")
names(color_key) <- levels(lindata$ds)

shape_key = c(2,1,0,18,20,17,15,16,62,4)
names(shape_key) <- levels(lindata$ds)

line_key = c(2,2,2,1,1,1,1,1,4,1)
names(line_key) <- levels(lindata$ds)

# legend_pos=c(0.5,0.92)
# y_range_up = 2000

# Benchmark-specific plot formatting
legend_pos=c(0.637,0.78)
# y_range_up=300
y_name="Latency (s)"
y_range_down = 100

# Generate the plots
linchart<-ggplot(data=lindata,
                  aes(x=thread,y=latency,color=ds, shape=ds, linetype=ds))+
  geom_line()+xlab("Threads")+ylab(y_name)+geom_point(size=4)+
  scale_shape_manual(values=shape_key[names(shape_key) %in% lindata$ds])+
  scale_linetype_manual(values=line_key[names(line_key) %in% lindata$ds])+
  theme_bw()+ guides(shape=guide_legend(title=NULL))+ 
  guides(color=guide_legend(title=NULL))+
  guides(linetype=guide_legend(title=NULL))+
  scale_color_manual(values=color_key[names(color_key) %in% lindata$ds])+
  scale_x_continuous(breaks=c(1,10,20,30,40,50,60,70,80,90),
      minor_breaks=c(2,4,8,16,32,64))+
  scale_y_continuous(trans='log2',label=scientific_10,breaks=c(10,100,1000,1000,10000,100000),
                minor_breaks=c(20,30,40,50,60,70,80,90,200,300,400,500,600,700,800,900,2000,3000,4000,5000,6000,7000,8000,9000))+
  coord_cartesian(xlim = c(-1, 65))+
  theme(plot.margin = unit(c(.2,0,-1.5,0), "cm"))+
  theme(legend.position=legend_pos,
    legend.background = element_blank(),
    legend.key = element_rect(colour = NA, fill = "transparent"))+
  theme(text = element_text(size = 18))+
  theme(axis.title.y = element_text(margin = margin(t = 0, r = 5, b = 0, l = 2)))+
  theme(axis.title.x = element_text(hjust =-0.185,vjust = 12.7,margin = margin(t = 15, r = 0, b = 10, l = 0)))

# Save all four plots to separate PDFs
ggsave(filename = paste("./graph_",t,".pdf",sep=""),linchart,width=5.3, height = 4, units = "in", dpi=300, title = paste("graph_",t,".pdf",sep=""))
}
# width=15.95
