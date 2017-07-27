using System;
using System.IO;
using System.Xml;
using Badger.Simion;
using Caliburn.Micro;

namespace Badger.ViewModels
{
    abstract public class NestedConfigNode : ConfigNodeViewModel
    {
        //Children
        protected BindableCollection<ConfigNodeViewModel> m_children = new BindableCollection<ConfigNodeViewModel>();
        public BindableCollection<ConfigNodeViewModel> children
        {
            get { return m_children; }
            set { m_children = value; NotifyOfPropertyChange(() => children); }
        }


        public override void outputXML(StreamWriter writer, SaveMode mode, string leftSpace)
        {
            if (mode == SaveMode.AsExperiment || mode == SaveMode.AsExperimentalUnit)
                writer.Write(leftSpace + getXMLHeader());
            //output children. If we are only exporting forks, we don't want to add indentations
            if (mode == SaveMode.ForkHierarchy || mode == SaveMode.ForkValues)
                outputChildrenXML(writer, mode, leftSpace);
            else
                outputChildrenXML(writer, mode, leftSpace + "\t");

            if (mode == SaveMode.AsExperiment || mode == SaveMode.AsExperimentalUnit)
                writer.Write(leftSpace + getXMLFooter());
        }


        public void outputChildrenXML(StreamWriter writer, SaveMode mode, string leftSpace)
        {
            foreach (ConfigNodeViewModel child in m_children)
                child.outputXML(writer, mode, leftSpace);
        }

        public virtual string getXMLHeader() { return "<" + name + ">\n"; }

        public virtual string getXMLFooter() { return "</" + name + ">\n"; }


        protected void childrenInit(ExperimentViewModel parentExperiment, XmlNode classDefinition,
            string parentXPath, XmlNode configNode = null)
        {
            ConfigNodeViewModel childNode;
            XmlNode forkNode;
            if (classDefinition != null)
            {
                foreach (XmlNode child in classDefinition.ChildNodes)
                {
                    forkNode = getForkChild(child.Attributes[XMLConfig.nameAttribute].Value, configNode);
                    if (forkNode != null)
                    {
                        children.Add(new ForkedNodeViewModel(parentExperiment, this, child, forkNode));
                    }
                    else
                    {
                        childNode = ConfigNodeViewModel.getInstance(parentExperiment, this, child,
                            parentXPath, configNode);
                        if (childNode != null)
                            children.Add(childNode);
                    }
                }
            }
        }

        //this method is used to search for fork nodes
        private XmlNode getForkChild(string childName, XmlNode configNode)
        {
            if (configNode == null) return null;

            foreach (XmlNode configChildNode in configNode)
            {
                if (configChildNode.Name == XMLConfig.forkedNodeTag
                    && configChildNode.Attributes[XMLConfig.nameAttribute].Value == childName)
                {
                    return configChildNode;
                }
            }
            return null;
        }


        public override void forkChild(ConfigNodeViewModel forkedChild)
        {
            ForkedNodeViewModel newForkNode;
            if (m_parentExperiment != null)
            {
                //cross-reference
                newForkNode = new ForkedNodeViewModel(m_parentExperiment, forkedChild);

                int oldIndex = children.IndexOf(forkedChild);

                if (oldIndex >= 0)
                {
                    children.Remove(forkedChild);
                    children.Insert(oldIndex, newForkNode);
                }
            }
        }

        public override bool validate()
        {
            bIsValid = true;
            foreach (ConfigNodeViewModel child in children)
            {
                if (!child.validate())
                {
                    bIsValid = false;
                }
            }
            return bIsValid;
        }

        //Lambda Traverse functions
        public override int traverseRetInt(Func<ConfigNodeViewModel, int> simpleNodeFunc,
            Func<ConfigNodeViewModel, int> nestedNodeFunc)
        {
            return nestedNodeFunc(this);
        }
        public override void traverseDoAction(Action<ConfigNodeViewModel> simpleNodeFunc,
            Action<NestedConfigNode> nestedNodeAction)
        {
            nestedNodeAction(this);
        }

        public override int getNumForkCombinations()
        {
            int numForkCombinations = 1;
            foreach (ConfigNodeViewModel child in children)
                numForkCombinations *= child.getNumForkCombinations();
            return numForkCombinations;
        }

        public override void setForkCombination(ref int id, ref string combinationName)
        {
            foreach (ConfigNodeViewModel child in children)
                child.setForkCombination(ref id, ref combinationName);
        }

        public override void onRemoved()
        {
            foreach (ConfigNodeViewModel child in children) child.onRemoved();
        }
    }
}