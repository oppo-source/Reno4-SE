#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <soc/oppo/oppo_project_oldcdt.h>

#include <linux/io.h>
#include <linux/of.h>
#ifdef CONFIG_MTK_SECURITY_SW_SUPPORT
#include <sec_boot_lib.h>
#endif
#include <linux/syscalls.h>
#include <soc/oppo/oppo_project.h>

/////////////////////////////////////////////////////////////
static struct proc_dir_entry *oppoVersion = NULL;
static ProjectInfoCDTType_oldcdt *format = NULL;

//#ifdef VENDOR_EDIT
//Zhou.Zheng@CN.NFC.Basic.Hardware,2674926, 2019/12/16, Add for : ST NXP chip common software
//ZhengZhou@CONNECTIVITY.NFC.Basic.Hardware,2674926, 2020/5/12, Add for : remove ese feature for device that do not support ese
static const char* nfc_feature = "nfc_feature";
static const char* feature_src = "/vendor/etc/nfc/com.oppo.nfc_feature.xml";
static const char* feature_src_no_ese = "/vendor/etc/nfc/com.oppo.nfc_feature_no_ese.xml";
//#endif /* VENDOR_EDIT */

static ProjectInfoCDTType_oldcdt projectInfo = {
	.nProject		= 0,
	.nModem			= 0,
	.nOperator		= 0,
	.nPCBVersion	        = 0,
	.nENGVersion            = 0,
	.isConfidential         = 1,
};

static int init_project_version_oldcdt(void)
{
	struct device_node *np = NULL;
	int ret = 0;

	if (format) {
		//pr_err("%s has inited\n", __func__);
		return 0;
	}
	printk("init_project_version_oldcdt start\n");

	np = of_find_node_by_name(NULL, "oppo_project");
	if(!np){
		printk("init_project_version error1");
		return -1;
	}

	ret = of_property_read_u32(np,"nProject",&(projectInfo.nProject));
	if(ret)
	{
		printk("init_project_version error2");
		return -1;
	}

	ret = of_property_read_u32(np,"nModem",&(projectInfo.nModem));
	if(ret)
	{
		printk("init_project_version error3");
		return -1;
	}

	ret = of_property_read_u32(np,"nOperator",&(projectInfo.nOperator));
	if(ret)
	{
		printk("init_project_version error4");
		return -1;
	}

	ret = of_property_read_u32(np,"nPCBVersion",&(projectInfo.nPCBVersion));
	if(ret)
	{
		printk("init_project_version error5");
		return -1;
	}

	ret = of_property_read_u32(np,"nENGVersion",&(projectInfo.nENGVersion));
	if(ret)
	{
		printk("init_project_version error6");
		//return -1;
	}

	ret = of_property_read_u32(np,"isConfidential",&(projectInfo.isConfidential));
	if(ret)
	{
		printk("init_project_version error7");
		//return -1;
	}
	format = &projectInfo;
	printk("KE oldcdt Version Info :Project(%d) Modem(%d) Operator(%d) PCB(%d) ENGVersion(%d) Confidential(%d)\n",
		format->nProject,format->nModem,format->nOperator,format->nPCBVersion, format->nENGVersion, format->isConfidential);

	return 0;
}


unsigned int get_project_oldcdt(void)
{
	if (format == NULL) {
		oppo_project_init_oldcdt();
	}
	if(format)
		return format->nProject;
	else
		return 0;
}

/* xiang.fei@PSW.MM.AudioDriver.Machine, 2018/05/28, Add for kernel driver */
EXPORT_SYMBOL(get_project_oldcdt);

unsigned int is_project_oldcdt(int project )
{
	return (get_project_oldcdt() == project?1:0);
}

unsigned int get_PCB_Version_oldcdt(void)
{
	if(format)
		return format->nPCBVersion;
	return 0;
}

unsigned int get_Modem_Version_oldcdt(void)
{
	if(format)
		return format->nModem;
	return 0;
}

unsigned int get_Operator_Version_oldcdt(void)
{
	if (format == NULL) {
		oppo_project_init_oldcdt();
	}
	if(format)
		return format->nOperator;
	return 0;
}

unsigned int get_eng_version_oldcdt(void)
{
	if(format)
		return format->nENGVersion;
	return 0;
}

bool oppo_daily_build_oldcdt(void)
{
	return false;
}

int is_confidential_oldcdt(void)
{
	if(format)
		return format->isConfidential;
	return 1;
}

//this module just init for creat files to show which version
static ssize_t prjVersion_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;
	len = sprintf(page,"%d",get_project_oldcdt());

	if(len > *off)
		len -= *off;
	else
		len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

static struct file_operations prjVersion_proc_fops = {
	.read = prjVersion_read_proc,
	.write = NULL,
};


static ssize_t pcbVersion_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page,"%d",get_PCB_Version_oldcdt());

	if(len > *off)
	   len -= *off;
	else
	   len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);

}

static struct file_operations pcbVersion_proc_fops = {
	.read = pcbVersion_read_proc,
};

static ssize_t engVersion_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page,"%d",get_eng_version_oldcdt());

	if(len > *off)
	   len -= *off;
	else
	   len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);

}

static struct file_operations engVersion_proc_fops = {
	.read = engVersion_read_proc,
};

static ssize_t is_confidential_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page,"%d",is_confidential_oldcdt());

	if(len > *off)
	   len -= *off;
	else
	   len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);

}

static struct file_operations isConfidential_proc_fops = {
	.read = is_confidential_read_proc,
};

static ssize_t operatorName_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page,"%d",get_Operator_Version_oldcdt());

	if(len > *off)
	   len -= *off;
	else
	   len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);

}

static struct file_operations operatorName_proc_fops = {
	.read = operatorName_read_proc,
};


static ssize_t modemType_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;

	len = sprintf(page,"%d",get_Modem_Version_oldcdt());

	if(len > *off)
	   len -= *off;
	else
	   len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);

}

static struct file_operations modemType_proc_fops = {
	.read = modemType_read_proc,
};

static ssize_t secureType_read_proc(struct file *file, char __user *buf,
		size_t count,loff_t *off)
{
	char page[256] = {0};
	int len = 0;
        #ifndef VENDOR_EDIT
	//LiBin@Prd6.BaseDrv, 2016/11/03,, Add for MTK secureType node
	//void __iomem *oem_config_base;
	uint32_t secure_oem_config = 0;

	//oem_config_base = ioremap(0x58034 , 10);
	//secure_oem_config = __raw_readl(oem_config_base);
	//iounmap(oem_config_base);
        printk(KERN_EMERG "lycan test secure_oem_config 0x%x\n", secure_oem_config);
        #else
#ifdef CONFIG_MTK_SECURITY_SW_SUPPORT
	int secure_oem_config = 0;
	secure_oem_config = sec_schip_enabled();
	printk(KERN_EMERG "lycan test secure_oem_config %d\n", secure_oem_config);
#else
	uint32_t secure_oem_config = 0;
#endif
        #endif /* VENDOR_EDIT */

	len = sprintf(page,"%d", secure_oem_config);

	if(len > *off)
	   len -= *off;
	else
	   len = 0;

	if(copy_to_user(buf,page,(len < count ? len : count))){
	   return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);

}

//#ifdef VENDOR_EDIT
//Zhou.Zheng@CN.NFC.Basic.Hardware,2674926, 2019/12/16, Add for : ST NXP chip common software
//Zhou.Zheng@CN.NFC.Basic.Hardware,2714107, 2020/04/13, Modify for : add 5G-B device
//ZhengZhou@CONNECTIVITY.NFC.Basic.Hardware,2674926, 2020/5/12, Add for : remove ese feature for device that do not support ese
#ifdef VENDOR_EDIT
static int __init update_feature(void)
{
	unsigned int project_name;
	unsigned int nfc_projects[] = {19165, 19166, 19133, 19040, 20003};
	int nfc_project_count = sizeof(nfc_projects) / sizeof(unsigned int);
	unsigned int nfc_projects_no_ese[] = {19420, 20200};
	int nfc_project_count_no_ese = sizeof(nfc_projects_no_ese) / sizeof(unsigned int);
	mm_segment_t fs;
	fs = get_fs();
	project_name = get_project();
	pr_err("update_feature, project name = [%d]", project_name);
	// pr_err("update_feature, OPPO Feature2 = [%d]", get_oppo_feature(1));
	set_fs(KERNEL_DS);
	if (oppoVersion) {
		for (int i = 0; i < nfc_project_count; i++) {
			if (project_name == nfc_projects[i]) {
				proc_symlink(nfc_feature, oppoVersion, feature_src);
				pr_err("update_feature, find nfc project = [%d]", project_name);
				break;
			}
		}
		for (int j = 0; j < nfc_project_count_no_ese; j++) {
			if (project_name == nfc_projects_no_ese[j]) {
				proc_symlink(nfc_feature, oppoVersion, feature_src_no_ese);
				pr_err("update_feature_no_ese, find nfc project = [%d]", project_name);
				break;
			}
		}
	}
	set_fs(fs);
	return 0;
}
late_initcall(update_feature);
#endif
//#endif /* VENDOR_EDIT */

static struct file_operations secureType_proc_fops = {
	.read = secureType_read_proc,
};

/*Yang.Tan@BSP.Fingerprint.Secure 2018/12/17 Add serialID for fastboot unlock*/
#define SERIALNO_LEN 16
extern char *saved_command_line;
static ssize_t serialID_read_proc(struct file *file, char __user *buf,
                size_t count, loff_t *off)
{
        char page[256] = {0};
        int len = 0;
        char * ptr;
        char serialno[SERIALNO_LEN+1] = {0};

        ptr = strstr(saved_command_line, "androidboot.serialno=");
        ptr += strlen("androidboot.serialno=");
        strncpy(serialno, ptr, SERIALNO_LEN);
        serialno[SERIALNO_LEN] = '\0';

        len = sprintf(page, "0x%s", serialno);
        if (len > *off) {
                len -= *off;
        }
        else{
                len = 0;
        }

        if (copy_to_user(buf, page, (len < count ? len : count))) {
                return -EFAULT;
        }

        *off += len < count ? len : count;
        return (len < count ? len : count);
}

struct file_operations serialID_proc_fops = {
        .read = serialID_read_proc,
};

int oppo_project_init_oldcdt(void)
{
	int ret = 0;
	struct proc_dir_entry *pentry;

	if(oppoVersion) //if oppoVersion is not null means this proc dir has inited
	{
		//pr_info("%s has inited\n", __func__);
		return ret;
	}
	
	ret = init_project_version_oldcdt();
	if (ret) {
		pr_err("%s init_project fail, ret:%d\n", __func__, ret);
		goto init_project_fail;
	}

	oppoVersion =  proc_mkdir("oppoVersion", NULL);
	if(!oppoVersion) {
		pr_err("can't create oppoVersion proc\n");
		goto ERROR_INIT_VERSION;
	}

	pentry = proc_create("prjVersion", S_IRUGO, oppoVersion, &prjVersion_proc_fops);
	if(!pentry) {
		pr_err("create prjVersion proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	
	pentry = proc_create("pcbVersion", S_IRUGO, oppoVersion, &pcbVersion_proc_fops);
	if(!pentry) {
		pr_err("create pcbVersion proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("engVersion", S_IRUGO, oppoVersion, &engVersion_proc_fops);
	if(!pentry) {
		pr_err("create engVersion proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("isConfidential", S_IRUGO, oppoVersion, &isConfidential_proc_fops);
	if(!pentry) {
		pr_err("create is_confidential proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("operatorName", S_IRUGO, oppoVersion, &operatorName_proc_fops);
	if(!pentry) {
		pr_err("create operatorName proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("modemType", S_IRUGO, oppoVersion, &modemType_proc_fops);
	if(!pentry) {
		pr_err("create modemType proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	pentry = proc_create("secureType", S_IRUGO, oppoVersion, &secureType_proc_fops);
	if(!pentry) {
		pr_err("create secureType proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
/*Yang.Tan@BSP.Fingerprint.Secure 2018/12/17 Add serialID for fastboot unlock*/
	pentry = proc_create("serialID", S_IRUGO, oppoVersion, &serialID_proc_fops);
	if(!pentry) {
		pr_err("create serialID proc failed.\n");
		goto ERROR_INIT_VERSION;
	}
	pr_err("%s success\n", __func__);
	return ret;
ERROR_INIT_VERSION:
	//remove_proc_entry("oppoVersion", NULL);
init_project_fail:
	oppoVersion = NULL;
	format = NULL;
	return -ENOENT;
}

//core_initcall(boot_oppo_project_core);

MODULE_DESCRIPTION("OPPO project oldcdt version");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Joshua <gyx@oppo.com>");
