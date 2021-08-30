library(plyr)
library(ggplot2)

scientific_10 <- function(x) {
  parse(text=gsub("1e\\+", "10^", scales::scientific_format()(x)))
}
# for memcached
tests<-c("")
for (t in tests){
lindata<-do.call(rbind,lapply(paste("./ycsbc_a",t,".csv",sep=""),read.csv,header=TRUE,row.names=NULL))

ddply(.data=lindata,.(option,thread),mutate,mops= mean(kops)/1000)->lindata
lindata$option<-as.factor(gsub("dram","DRAM (T)",lindata$option))
lindata$option<-as.factor(gsub("nvm","Montage (T)",lindata$option))
lindata$option<-as.factor(gsub("montage","Montage",lindata$option))
lindata$option <- factor(lindata$option, levels=c("DRAM (T)", "Montage (T)", "Montage"))

color_key = c("#12E1EA","#FF69B4",
               "#C11B14", "#FF69B4", "#FF8C00", "#F86945")
names(color_key) <- levels(lindata$option)

shape_key = c(2,1,18,0,3,62)
names(shape_key) <- levels(lindata$option)

line_key = c(4,2,1,5,5,4)
names(line_key) <- levels(lindata$option)

#####################################
#### Begin charts for Throughput ####
#####################################

legend_pos=c(0.769,0.77)
y_range_up=2.2
y_name="Throughput (Mops/s)"

# Generate the plots
linchart<-ggplot(data=lindata,
                  aes(x=thread,y=mops,color=option, shape=option, linetype=option))+
  geom_line()+xlab("Threads")+ylab(y_name)+geom_point(size=4)+
  scale_shape_manual(values=shape_key[names(shape_key) %in% lindata$option])+
  scale_linetype_manual(values=line_key[names(line_key) %in% lindata$option])+
  theme_bw()+ guides(shape=guide_legend(title=NULL))+ 
  guides(color=guide_legend(title=NULL))+
  guides(linetype=guide_legend(title=NULL))+
  scale_color_manual(values=color_key[names(color_key) %in% lindata$option])+
  scale_x_continuous(breaks=c(0,10,20,30,40,50,60,70,80,90),minor_breaks=c(-10))+
  coord_cartesian(xlim = c(0,90), ylim = c(0, y_range_up))+
  theme(plot.margin = unit(c(.2,0,-1.5,0), "cm"))+
  theme(legend.position=legend_pos,
    legend.background = element_blank(),
    legend.key = element_rect(colour = NA, fill = "transparent"))+
  theme(text = element_text(size = 18))+
  theme(axis.title.y = element_text(margin = margin(t = 0, r = 5, b = 0, l = 2)))+
  theme(axis.title.x = element_text(hjust =-0.105,vjust = 8.7,margin = margin(t = 15, r = 0, b = 10, l = 0)))

# Save all four plots to separate PDFs
ggsave(filename = paste("./threadcached_ycsba",t,".pdf",sep=""),linchart,width=8, height = 4, units = "in", dpi=300, title = paste("threadcached_ycsba",t,".pdf",sep=""))
}
